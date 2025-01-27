/*************************************************************************************************
 * File hash database
 *                                                      Copyright (C) 2009-2010 Mikio Hirabayashi
 * This file is part of Kyoto Cabinet.
 * This program is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, either version
 * 3 of the License, or any later version.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *************************************************************************************************/


#ifndef _KCHASHDB_H                      // duplication check
#define _KCHASHDB_H

#include <kccommon.h>
#include <kcutil.h>
#include <kcdb.h>
#include <kcthread.h>
#include <kcfile.h>
#include <kccompress.h>
#include <kccompare.h>
#include <kcmap.h>

namespace kyotocabinet {                 // common namespace


/**
 * Constants for implementation.
 */
namespace {
const char HDBMAGICDATA[] = "KC\n";      ///< magic data of the file
const char HDBCHKSUMSEED[] = "__kyotocabinet__";  ///< seed of the module checksum
const int64_t HDBMOFFLIBVER = 4;         ///< offset of the library version
const int64_t HDBMOFFLIBREV = 5;         ///< offset of the library revision
const int64_t HDBMOFFFMTVER = 6;         ///< offset of the format revision
const int64_t HDBMOFFCHKSUM = 7;         ///< offset of the module checksum
const int64_t HDBMOFFTYPE = 8;           ///< offset of the database type
const int64_t HDBMOFFAPOW = 9;           ///< offset of the alignment power
const int64_t HDBMOFFFPOW = 10;          ///< offset of the free block pool power
const int64_t HDBMOFFOPTS = 11;          ///< offset of the options
const int64_t HDBMOFFBNUM = 16;          ///< offset of the bucket number
const int64_t HDBMOFFFLAGS = 24;         ///< offset of the status flags
const int64_t HDBMOFFCOUNT = 32;         ///< offset of the record number
const int64_t HDBMOFFSIZE = 40;          ///< offset of the file size
const int64_t HDBMOFFOPAQUE = 48;        ///< offset of the opaque data
const int64_t HDBHEADSIZ = 64;           ///< size of the header
const int32_t HDBFBPWIDTH = 6;           ///< width of the free block
const int32_t HDBWIDTHLARGE = 6;         ///< large width of the record address
const int32_t HDBWIDTHSMALL = 4;         ///< small width of the record address
const size_t HDBRECBUFSIZ = 48;          ///< size of the record buffer
const size_t HDBIOBUFSIZ = 1024;         ///< size of the IO buffer
const int32_t HDBRLOCKSLOT = 64;         ///< number of slots of the record lock
const uint8_t HDBDEFAPOW = 3;            ///< default alignment power
const uint8_t HDBMAXAPOW = 15;           ///< maximum alignment power
const uint8_t HDBDEFFPOW = 10;           ///< default free block pool power
const uint8_t HDBMAXFPOW = 20;           ///< maximum free block pool power
const int64_t HDBDEFBNUM = 1048583LL;    ///< default bucket number
const int64_t HDBDEFMSIZ = 64LL << 20;   ///< default size of the memory-mapped region
const uint8_t HDBRECMAGIC = 0xcc;        ///< magic data for record
const uint8_t HDBPADMAGIC = 0xee;        ///< magic data for padding
const uint8_t HDBFBMAGIC = 0xdd;         ///< magic data for free block
const int32_t HDBDFRGMAX = 512;          ///< maximum unit of auto defragmentation
const int32_t HDBDFRGCEF = 2;            ///< coefficient of auto defragmentation
const char* HDBTMPPATHEXT = "tmpkch";    ///< extension of the temporary file
}


/**
 * File hash database.
 */
class HashDB : public FileDB {
public:
  class Cursor;
private:
  struct Record;
  struct FreeBlock;
  struct FreeBlockComparator;
  class Repeater;
  class Transactor;
  friend class TreeDB;
  /** An alias of set of free blocks. */
  typedef std::set<FreeBlock> FBP;
  /** An alias of list of cursors. */
  typedef std::list<Cursor*> CursorList;
public:
  /**
   * Cursor to indicate a record.
   */
  class Cursor : public FileDB::Cursor {
    friend class HashDB;
  public:
    /**
     * Constructor.
     * @param db the container database object.
     */
    explicit Cursor(HashDB* db) : db_(db), off_(0), end_(0) {
      _assert_(db);
      ScopedSpinRWLock lock(&db_->mlock_, true);
      db_->curs_.push_back(this);
    }
    /**
     * Destructor.
     */
    virtual ~Cursor() {
      _assert_(true);
      if (!db_) return;
      ScopedSpinRWLock lock(&db_->mlock_, true);
      db_->curs_.remove(this);
    }
    /**
     * Accept a visitor to the current record.
     * @param visitor a visitor object.
     * @param writable true for writable operation, or false for read-only operation.
     * @param step true to move the cursor to the next record, or false for no move.
     * @return true on success, or false on failure.
     * @note the operation for each record is performed atomically and other threads accessing
     * the same record are blocked.
     */
    virtual bool accept(Visitor* visitor, bool writable = true, bool step = false) {
      _assert_(visitor);
      ScopedSpinRWLock lock(&db_->mlock_, true);
      if (db_->omode_ == 0) {
        db_->set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
        return false;
      }
      if (writable && !(db_->writer_)) {
        db_->set_error(__FILE__, __LINE__, Error::NOPERM, "permission denied");
        return false;
      }
      if (off_ < 1) {
        db_->set_error(__FILE__, __LINE__, Error::NOREC, "no record");
        return false;
      }
      Record rec;
      char rbuf[HDBRECBUFSIZ];
      if (!step_impl(&rec, rbuf, 0)) return false;
      if (!rec.vbuf && !db_->read_record_body(&rec)) {
        delete[] rec.bbuf;
        return false;
      }
      const char* vbuf = rec.vbuf;
      size_t vsiz = rec.vsiz;
      char* zbuf = NULL;
      size_t zsiz = 0;
      if (db_->comp_) {
        zbuf = db_->comp_->decompress(vbuf, vsiz, &zsiz);
        if (!zbuf) {
          db_->set_error(__FILE__, __LINE__, Error::SYSTEM, "data decompression failed");
          delete[] rec.bbuf;
          return false;
        }
        vbuf = zbuf;
        vsiz = zsiz;
      }
      vbuf = visitor->visit_full(rec.kbuf, rec.ksiz, vbuf, vsiz, &vsiz);
      delete[] zbuf;
      if (vbuf == Visitor::REMOVE) {
        uint64_t hash = db_->hash_record(rec.kbuf, rec.ksiz);
        uint32_t pivot = db_->fold_hash(hash);
        int64_t bidx = hash % db_->bnum_;
        Repeater repeater(Visitor::REMOVE, 0);
        if (!db_->accept_impl(rec.kbuf, rec.ksiz, &repeater, bidx, pivot, true)) {
          delete[] rec.bbuf;
          return false;
        }
        delete[] rec.bbuf;
      } else if (vbuf == Visitor::NOP) {
        delete[] rec.bbuf;
        if (step) {
          if (step_impl(&rec, rbuf, 1)) {
            delete[] rec.bbuf;
          } else if (db_->error().code() != Error::NOREC) {
            return false;
          }
        }
      } else {
        zbuf = NULL;
        zsiz = 0;
        if (db_->comp_) {
          zbuf = db_->comp_->compress(vbuf, vsiz, &zsiz);
          if (!zbuf) {
            db_->set_error(__FILE__, __LINE__, Error::SYSTEM, "data compression failed");
            delete[] rec.bbuf;
            return false;
          }
          vbuf = zbuf;
          vsiz = zsiz;
        }
        size_t rsiz = db_->calc_record_size(rec.ksiz, vsiz);
        if (rsiz <= rec.rsiz) {
          rec.psiz = rec.rsiz - rsiz;
          rec.vsiz = vsiz;
          rec.vbuf = vbuf;
          if (!db_->adjust_record(&rec) || !db_->write_record(&rec, true)) {
            delete[] zbuf;
            delete[] rec.bbuf;
            return false;
          }
          delete[] zbuf;
          delete[] rec.bbuf;
          if (step) {
            if (step_impl(&rec, rbuf, 1)) {
              delete[] rec.bbuf;
            } else if (db_->error().code() != Error::NOREC) {
              return false;
            }
          }
        } else {
          uint64_t hash = db_->hash_record(rec.kbuf, rec.ksiz);
          uint32_t pivot = db_->fold_hash(hash);
          int64_t bidx = hash % db_->bnum_;
          Repeater repeater(vbuf, vsiz);
          if (!db_->accept_impl(rec.kbuf, rec.ksiz, &repeater, bidx, pivot, true)) {
            delete[] zbuf;
            delete[] rec.bbuf;
            return false;
          }
          delete[] zbuf;
          delete[] rec.bbuf;
        }
      }
      if (db_->dfunit_ > 0 && db_->frgcnt_ >= db_->dfunit_) {
        if (!db_->defrag_impl(db_->dfunit_ * HDBDFRGCEF)) return false;
        db_->frgcnt_ -= db_->dfunit_;
      }
      return true;
    }
    /**
     * Jump the cursor to the first record.
     * @return true on success, or false on failure.
     */
    virtual bool jump() {
      _assert_(true);
      ScopedSpinRWLock lock(&db_->mlock_, true);
      if (db_->omode_ == 0) {
        db_->set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
        return false;
      }
      off_ = 0;
      if (db_->lsiz_ <= db_->roff_) {
        db_->set_error(__FILE__, __LINE__, Error::NOREC, "no record");
        return false;
      }
      off_ = db_->roff_;
      end_ = db_->lsiz_;
      return true;
    }
    /**
     * Jump the cursor onto a record.
     * @param kbuf the pointer to the key region.
     * @param ksiz the size of the key region.
     * @return true on success, or false on failure.
     */
    virtual bool jump(const char* kbuf, size_t ksiz) {
      _assert_(kbuf && ksiz <= MEMMAXSIZ);
      ScopedSpinRWLock lock(&db_->mlock_, true);
      if (db_->omode_ == 0) {
        db_->set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
        return false;
      }
      off_ = 0;
      uint64_t hash = db_->hash_record(kbuf, ksiz);
      uint32_t pivot = db_->fold_hash(hash);
      int64_t bidx = hash % db_->bnum_;
      int64_t off = db_->get_bucket(bidx);
      if (off < 0) return false;
      Record rec;
      char rbuf[HDBRECBUFSIZ];
      while (off > 0) {
        rec.off = off;
        if (!db_->read_record(&rec, rbuf)) return false;
        if (rec.psiz == UINT16_MAX) {
          db_->set_error(__FILE__, __LINE__, Error::BROKEN, "free block in the chain");
          db_->report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
                      (long)db_->psiz_, (long)rec.off, (long)db_->file_.size());
          return false;
        }
        uint32_t tpivot = db_->linear_ ? pivot :
          db_->fold_hash(db_->hash_record(rec.kbuf, rec.ksiz));
        if (pivot > tpivot) {
          delete[] rec.bbuf;
          off = rec.left;
        } else if (pivot < tpivot) {
          delete[] rec.bbuf;
          off = rec.right;
        } else {
          int32_t kcmp = db_->compare_keys(kbuf, ksiz, rec.kbuf, rec.ksiz);
          if (db_->linear_ && kcmp != 0) kcmp = 1;
          if (kcmp > 0) {
            delete[] rec.bbuf;
            off = rec.left;
          } else if (kcmp < 0) {
            delete[] rec.bbuf;
            off = rec.right;
          } else {
            delete[] rec.bbuf;
            off_ = off;
            end_ = db_->lsiz_;
            return true;
          }
        }
      }
      db_->set_error(__FILE__, __LINE__, Error::NOREC, "no record");
      return false;
    }
    /**
     * Jump the cursor to a record.
     * @note Equal to the original Cursor::jump method except that the parameter is std::string.
     */
    virtual bool jump(const std::string& key) {
      _assert_(true);
      return jump(key.c_str(), key.size());
    }
    /**
     * Step the cursor to the next record.
     * @return true on success, or false on failure.
     */
    virtual bool step() {
      _assert_(true);
      ScopedSpinRWLock lock(&db_->mlock_, true);
      if (db_->omode_ == 0) {
        db_->set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
        return false;
      }
      if (off_ < 1) {
        db_->set_error(__FILE__, __LINE__, Error::NOREC, "no record");
        return false;
      }
      bool err = false;
      Record rec;
      char rbuf[HDBRECBUFSIZ];
      if (step_impl(&rec, rbuf, 1)) {
        delete[] rec.bbuf;
      } else {
        err = true;
      }
      return !err;
    }
    /**
     * Get the database object.
     * @return the database object.
     */
    virtual HashDB* db() {
      _assert_(true);
      return db_;
    }
  private:
    /**
     * Step the cursor to the next record.
     * @param rec the record structure.
     * @param rbuf the working buffer.
     * @param skip the number of skipping blocks.
     * @return true on success, or false on failure.
     */
    bool step_impl(Record* rec, char* rbuf, int64_t skip) {
      _assert_(rec && rbuf && skip >= 0);
      if (off_ >= end_) {
        db_->set_error(__FILE__, __LINE__, Error::BROKEN, "cursor after the end");
        db_->report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
                    (long)db_->psiz_, (long)rec->off, (long)db_->file_.size());
        return false;
      }
      while (off_ < end_) {
        rec->off = off_;
        if (!db_->read_record(rec, rbuf)) return false;
        skip--;
        if (rec->psiz == UINT16_MAX) {
          off_ += rec->rsiz;
        } else {
          if (skip < 0) return true;
          delete[] rec->bbuf;
          off_ += rec->rsiz;
        }
      }
      db_->set_error(__FILE__, __LINE__, Error::NOREC, "no record");
      off_ = 0;
      return false;
    }
    /** Dummy constructor to forbid the use. */
    Cursor(const Cursor&);
    /** Dummy Operator to forbid the use. */
    Cursor& operator =(const Cursor&);
    /** The inner database. */
    HashDB* db_;
    /** The current offset. */
    int64_t off_;
    /** The end offset. */
    int64_t end_;
  };
  /**
   * Tuning Options.
   */
  enum Option {
    TSMALL = 1 << 0,                     ///< use 32-bit addressing
    TLINEAR = 1 << 1,                    ///< use linear collision chaining
    TCOMPRESS = 1 << 2                   ///< compress each record
  };
  /**
   * Status flags.
   */
  enum Flag {
    FOPEN = 1 << 0,                      ///< whether opened
    FFATAL = 1 << 1                      ///< whether with fatal error
  };
  /**
   * Default constructor.
   */
  explicit HashDB() :
    mlock_(), rlock_(), flock_(), atlock_(), error_(), erstrm_(NULL), ervbs_(false),
    omode_(0), writer_(false), autotran_(false), autosync_(false), reorg_(false), trim_(false),
    file_(), fbp_(), curs_(), path_(""),
    libver_(LIBVER), librev_(LIBREV), fmtver_(FMTVER), chksum_(0), type_(TYPEHASH),
    apow_(HDBDEFAPOW), fpow_(HDBDEFFPOW), opts_(0), bnum_(HDBDEFBNUM),
    flags_(0), flagopen_(false), count_(0), lsiz_(0), psiz_(0), opaque_(),
    msiz_(HDBDEFMSIZ), dfunit_(0), embcomp_(&ZLIBRAWCOMP),
    align_(0), fbpnum_(0), width_(0), linear_(false),
    comp_(NULL), rhsiz_(0), boff_(0), roff_(0), dfcur_(0), frgcnt_(0),
    tran_(false), trhard_(false), trfbp_() {
    _assert_(true);
  }
  /**
   * Destructor.
   * @note If the database is not closed, it is closed implicitly.
   */
  virtual ~HashDB() {
    _assert_(true);
    if (omode_ != 0) close();
    if (curs_.size() > 0) {
      CursorList::const_iterator cit = curs_.begin();
      CursorList::const_iterator citend = curs_.end();
      while (cit != citend) {
        Cursor* cur = *cit;
        cur->db_ = NULL;
        cit++;
      }
    }
  }
  /**
   * Accept a visitor to a record.
   * @param kbuf the pointer to the key region.
   * @param ksiz the size of the key region.
   * @param visitor a visitor object.
   * @param writable true for writable operation, or false for read-only operation.
   * @return true on success, or false on failure.
   * @note the operation for each record is performed atomically and other threads accessing the
   * same record are blocked.
   */
  virtual bool accept(const char* kbuf, size_t ksiz, Visitor* visitor, bool writable = true) {
    _assert_(kbuf && ksiz <= MEMMAXSIZ && visitor);
    mlock_.lock_reader();
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      mlock_.unlock();
      return false;
    }
    if (writable && !writer_) {
      set_error(__FILE__, __LINE__, Error::NOPERM, "permission denied");
      mlock_.unlock();
      return false;
    }
    uint64_t hash = hash_record(kbuf, ksiz);
    uint32_t pivot = fold_hash(hash);
    int64_t bidx = hash % bnum_;
    size_t lidx = bidx % HDBRLOCKSLOT;
    if (writable) {
      rlock_.lock_writer(lidx);
    } else {
      rlock_.lock_reader(lidx);
    }
    bool err = false;
    if (!accept_impl(kbuf, ksiz, visitor, bidx, pivot, false)) err = true;
    rlock_.unlock(lidx);
    if (!err && dfunit_ > 0 && frgcnt_ >= dfunit_ && mlock_.promote()) {
      int64_t unit = frgcnt_;
      if (unit >= dfunit_) {
        if (unit > HDBDFRGMAX) unit = HDBDFRGMAX;
        if (!defrag_impl(unit * HDBDFRGCEF)) err = true;
        frgcnt_ -= unit;
      }
    }
    mlock_.unlock();
    return !err;
  }
  /**
   * Iterate to accept a visitor for each record.
   * @param visitor a visitor object.
   * @param writable true for writable operation, or false for read-only operation.
   * @return true on success, or false on failure.
   * @note the whole iteration is performed atomically and other threads are blocked.
   */
  virtual bool iterate(Visitor *visitor, bool writable = true) {
    _assert_(visitor);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return false;
    }
    if (writable && !writer_) {
      set_error(__FILE__, __LINE__, Error::NOPERM, "permission denied");
      return false;
    }
    bool err = false;
    if (!iterate_impl(visitor)) err = true;
    return !err;
  }
  /**
   * Get the last happened error.
   * @return the last happened error.
   */
  virtual Error error() const {
    _assert_(true);
    return error_;
  }
  /**
   * Set the error information.
   * @param code an error code.
   * @param message a supplement message.
   */
  virtual void set_error(Error::Code code, const char* message) {
    _assert_(message);
    error_->set(code, message);
    if (code == Error::BROKEN || code == Error::SYSTEM) flags_ |= FFATAL;
  }
  /**
   * Open a database file.
   * @param path the path of a database file.
   * @param mode the connection mode.  HashDB::OWRITER as a writer, HashDB::OREADER as a
   * reader.  The following may be added to the writer mode by bitwise-or: HashDB::OCREATE,
   * which means it creates a new database if the file does not exist, HashDB::OTRUNCATE, which
   * means it creates a new database regardless if the file exists, HashDB::OAUTOTRAN, which
   * means each updating operation is performed in implicit transaction, HashDB::OAUTOSYNC,
   * which means each updating operation is followed by implicit synchronization with the file
   * system.  The following may be added to both of the reader mode and the writer mode by
   * bitwise-or: HashDB::ONOLOCK, which means it opens the database file without file locking,
   * HashDB::OTRYLOCK, which means locking is performed without blocking, HashDB::ONOREPAIR,
   * which means the database file is not repaired implicitly even if file destruction is
   * detected.
   * @return true on success, or false on failure.
   * @note Every opened database must be closed by the HashDB::close method when it is no
   * longer in use.
   */
  virtual bool open(const std::string& path, uint32_t mode = OWRITER | OCREATE) {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ != 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "already opened");
      return false;
    }
    writer_ = false;
    autotran_ = false;
    autosync_ = false;
    reorg_ = false;
    trim_ = false;
    uint32_t fmode = File::OREADER;
    if (mode & OWRITER) {
      writer_ = true;
      fmode = File::OWRITER;
      if (mode & OCREATE) fmode |= File::OCREATE;
      if (mode & OTRUNCATE) fmode |= File::OTRUNCATE;
      if (mode & OAUTOTRAN) autotran_ = true;
      if (mode & OAUTOSYNC) autosync_ = true;
    }
    if (mode & ONOLOCK) fmode |= File::ONOLOCK;
    if (mode & OTRYLOCK) fmode |= File::OTRYLOCK;
    if (!file_.open(path, fmode, msiz_)) {
      const char* emsg = file_.error();
      Error::Code code = Error::SYSTEM;
      if (std::strstr(emsg, "(permission denied)") || std::strstr(emsg, "(directory)")) {
        code = Error::NOPERM;
      } else if (std::strstr(emsg, "(file not found)") || std::strstr(emsg, "(invalid path)")) {
        code = Error::NOFILE;
      }
      set_error(__FILE__, __LINE__, code, emsg);
      return false;
    }
    if (file_.recovered()) report(__FILE__, __LINE__, "info", "recovered by the WAL file");
    if ((mode & OWRITER) && file_.size() < 1) {
      calc_meta();
      chksum_ = calc_checksum();
      lsiz_ = roff_;
      if (!file_.truncate(lsiz_)) {
        set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
        file_.close();
        return false;
      }
      if (!dump_meta()) {
        file_.close();
        return false;
      }
    }
    if (!load_meta()) {
      file_.close();
      return false;
    }
    calc_meta();
    uint8_t chksum = calc_checksum();
    if (chksum != chksum_) {
      set_error(__FILE__, __LINE__, Error::INVALID, "invalid module checksum");
      report(__FILE__, __LINE__, "info", "saved=%02X calculated=%02X",
             (unsigned)chksum, (unsigned)chksum_);
      file_.close();
      return false;
    }
    if (((flags_ & FOPEN) || (flags_ & FFATAL)) && !(mode & ONOREPAIR) && !(mode & ONOLOCK) &&
        !reorganize_file(path)) {
      file_.close();
      return false;
    }
    if (type_ == 0 || apow_ > HDBMAXAPOW || fpow_ > HDBMAXFPOW ||
        bnum_ < 1 || count_ < 0 || lsiz_ < roff_) {
      set_error(__FILE__, __LINE__, Error::BROKEN, "invalid meta data");
      report(__FILE__, __LINE__, "info", "type=0x%02X apow=%d fpow=%d bnum=%ld count=%ld"
             " lsiz=%ld fsiz=%ld", (unsigned)type_, (int)apow_, (int)fpow_, (long)bnum_,
             (long)count_, (long)lsiz_, (long)file_.size());
      file_.close();
      return false;
    }
    if (file_.size() < lsiz_) {
      set_error(__FILE__, __LINE__, Error::BROKEN, "inconsistent file size");
      report(__FILE__, __LINE__, "info", "lsiz=%ld fsiz=%ld", (long)lsiz_, (long)file_.size());
      file_.close();
      return false;
    }
    if (file_.size() != lsiz_ && !(mode & ONOREPAIR) && !(mode & ONOLOCK) && !trim_file(path)) {
      file_.close();
      return false;
    }
    if (mode & OWRITER) {
      if (!(flags_ & FOPEN) && !(flags_ & FFATAL) && !load_free_blocks()) {
        file_.close();
        return false;
      }
      if (!dump_empty_free_blocks()) {
        file_.close();
        return false;
      }
      if (!autotran_ && !set_flag(FOPEN, true)) {
        file_.close();
        return false;
      }
    }
    path_.append(path);
    omode_ = mode;
    return true;
  }
  /**
   * Close the database file.
   * @return true on success, or false on failure.
   */
  virtual bool close() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return false;
    }
    bool err = false;
    if (tran_ && !abort_transaction()) err = true;
    disable_cursors();
    if (writer_) {
      if (!dump_free_blocks()) err = true;
      if (!dump_meta()) err = true;
    }
    if (!file_.close()) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      err = true;
    }
    fbp_.clear();
    omode_ = 0;
    path_.clear();
    return !err;
  }
  /**
   * Synchronize updated contents with the file and the device.
   * @param hard true for physical synchronization with the device, or false for logical
   * synchronization with the file system.
   * @param proc a postprocessor object.  If it is NULL, no postprocessing is performed.
   * @return true on success, or false on failure.
   */
  virtual bool synchronize(bool hard = false, FileProcessor* proc = NULL) {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return false;
    }
    if (!writer_) {
      set_error(__FILE__, __LINE__, Error::NOPERM, "permission denied");
      return false;
    }
    rlock_.lock_reader_all();
    bool err = false;
    if (!synchronize_impl(hard, proc)) err = true;
    rlock_.unlock_all();
    return !err;
  }
  /**
   * Begin transaction.
   * @param hard true for physical synchronization with the device, or false for logical
   * synchronization with the file system.
   * @return true on success, or false on failure.
   */
  virtual bool begin_transaction(bool hard = false) {
    _assert_(true);
    for (double wsec = 1.0 / CLOCKTICK; true; wsec *= 2) {
      mlock_.lock_writer();
      if (omode_ == 0) {
        set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
        mlock_.unlock();
        return false;
      }
      if (!writer_) {
        set_error(__FILE__, __LINE__, Error::NOPERM, "permission denied");
        mlock_.unlock();
        return false;
      }
      if (!tran_) break;
      mlock_.unlock();
      if (wsec > 1.0) wsec = 1.0;
      Thread::sleep(wsec);
    }
    trhard_ = hard;
    if (!begin_transaction_impl()) {
      mlock_.unlock();
      return false;
    }
    tran_ = true;
    mlock_.unlock();
    return true;
  }
  /**
   * Try to begin transaction.
   * @param hard true for physical synchronization with the device, or false for logical
   * synchronization with the file system.
   * @return true on success, or false on failure.
   */
  virtual bool begin_transaction_try(bool hard = false) {
    _assert_(true);
    mlock_.lock_writer();
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      mlock_.unlock();
      return false;
    }
    if (!writer_) {
      set_error(__FILE__, __LINE__, Error::NOPERM, "permission denied");
      mlock_.unlock();
      return false;
    }
    if (tran_) {
      set_error(__FILE__, __LINE__, Error::LOGIC, "competition avoided");
      mlock_.unlock();
      return false;
    }
    trhard_ = hard;
    if (!begin_transaction_impl()) {
      mlock_.unlock();
      return false;
    }
    tran_ = true;
    mlock_.unlock();
    return true;
  }
  /**
   * End transaction.
   * @param commit true to commit the transaction, or false to abort the transaction.
   * @return true on success, or false on failure.
   */
  virtual bool end_transaction(bool commit = true) {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return false;
    }
    if (!tran_) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not in transaction");
      return false;
    }
    bool err = false;
    if (commit) {
      if (!commit_transaction()) err = true;
    } else {
      if (!abort_transaction()) err = true;
    }
    tran_ = false;
    return !err;
  }
  /**
   * Remove all records.
   * @return true on success, or false on failure.
   */
  virtual bool clear() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return false;
    }
    if (!writer_) {
      set_error(__FILE__, __LINE__, Error::NOPERM, "permission denied");
      return false;
    }
    disable_cursors();
    if (!file_.truncate(HDBHEADSIZ)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      return false;
    }
    fbp_.clear();
    bool err = false;
    flags_ = 0;
    flagopen_ = false;
    count_ = 0;
    lsiz_ = roff_;
    psiz_ = lsiz_;
    dfcur_ = roff_;
    std::memset(opaque_, 0, sizeof(opaque_));
    if (!file_.truncate(lsiz_)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      err = true;
    }
    if (!dump_meta()) err = true;
    if (!set_flag(FOPEN, true)) err = true;
    return true;
  }
  /**
   * Get the number of records.
   * @return the number of records, or -1 on failure.
   */
  virtual int64_t count() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return -1;
    }
    return count_;
  }
  /**
   * Get the size of the database file.
   * @return the size of the database file in bytes, or -1 on failure.
   */
  virtual int64_t size() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return -1;
    }
    return lsiz_;
  }
  /**
   * Get the path of the database file.
   * @return the path of the database file, or an empty string on failure.
   */
  virtual std::string path() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return "";
    }
    return path_;
  }
  /**
   * Get the miscellaneous status information.
   * @param strmap a string map to contain the result.
   * @return true on success, or false on failure.
   */
  virtual bool status(std::map<std::string, std::string>* strmap) {
    _assert_(strmap);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return false;
    }
    (*strmap)["type"] = "HashDB";
    (*strmap)["realtype"] = strprintf("%u", type_);
    (*strmap)["path"] = path_;
    (*strmap)["libver"] = strprintf("%u", libver_);
    (*strmap)["librev"] = strprintf("%u", librev_);
    (*strmap)["fmtver"] = strprintf("%u", fmtver_);
    (*strmap)["chksum"] = strprintf("%u", chksum_);
    (*strmap)["flags"] = strprintf("%u", flags_);
    (*strmap)["apow"] = strprintf("%u", apow_);
    (*strmap)["fpow"] = strprintf("%u", fpow_);
    (*strmap)["opts"] = strprintf("%u", opts_);
    (*strmap)["bnum"] = strprintf("%lld", (long long)bnum_);
    (*strmap)["msiz"] = strprintf("%lld", (long long)msiz_);
    (*strmap)["dfunit"] = strprintf("%lld", (long long)dfunit_);
    (*strmap)["frgcnt"] = strprintf("%lld", (long long)(frgcnt_ > 0 ? (int64_t)frgcnt_ : 0));
    (*strmap)["realsize"] = strprintf("%lld", (long long)file_.size());
    (*strmap)["recovered"] = strprintf("%d", file_.recovered());
    (*strmap)["reorganized"] = strprintf("%d", reorg_);
    if (strmap->count("fbpnum_used") > 0) {
      if (writer_) {
        (*strmap)["fbpnum_used"] = strprintf("%lld", (long long)fbp_.size());
      } else {
        if (!load_free_blocks()) return false;
        (*strmap)["fbpnum_used"] = strprintf("%lld", (long long)fbp_.size());
        fbp_.clear();
      }
    }
    if (strmap->count("bnum_used") > 0) {
      int64_t cnt = 0;
      for (int64_t i = 0; i < bnum_; i++) {
        if (get_bucket(i) > 0) cnt++;
      }
      (*strmap)["bnum_used"] = strprintf("%lld", (long long)cnt);
    }
    if (strmap->count("opaque") > 0)
      (*strmap)["opaque"] = std::string(opaque_, sizeof(opaque_));
    (*strmap)["count"] = strprintf("%lld", (long long)count_);
    (*strmap)["size"] = strprintf("%lld", (long long)lsiz_);
    return true;
  }
  /**
   * Create a cursor object.
   * @return the return value is the created cursor object.
   * @note Because the object of the return value is allocated by the constructor, it should be
   * released with the delete operator when it is no longer in use.
   */
  virtual Cursor* cursor() {
    _assert_(true);
    return new Cursor(this);
  }
  /**
   * Set the internal error reporter.
   * @param erstrm a stream object into which internal error messages are stored.
   * @param ervbs true to report all errors, or false to report fatal errors only.
   * @return true on success, or false on failure.
   */
  virtual bool tune_error_reporter(std::ostream* erstrm, bool ervbs) {
    _assert_(erstrm);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ != 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "already opened");
      return false;
    }
    erstrm_ = erstrm;
    ervbs_ = ervbs;
    return true;
  }
  /**
   * Set the power of the alignment of record size.
   * @param apow the power of the alignment of record size.
   * @return true on success, or false on failure.
   */
  virtual bool tune_alignment(int8_t apow) {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ != 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "already opened");
      return false;
    }
    apow_ = apow >= 0 ? apow : HDBDEFAPOW;
    if (apow_ > HDBMAXAPOW) apow_ = HDBMAXAPOW;
    return true;
  }
  /**
   * Set the power of the capacity of the free block pool.
   * @param fpow the power of the capacity of the free block pool.
   * @return true on success, or false on failure.
   */
  virtual bool tune_fbp(int8_t fpow) {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ != 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "already opened");
      return false;
    }
    fpow_ = fpow >= 0 ? fpow : HDBDEFFPOW;
    if (fpow_ > HDBMAXFPOW) fpow_ = HDBMAXFPOW;
    return true;
  }
  /**
   * Set the optional features.
   * @param opts the optional features by bitwise-or: HashDB::TSMALL to use 32-bit addressing,
   * HashDB::TLINEAR to use linear collision chaining, HashDB::TCOMPRESS to compress each record.
   * @return true on success, or false on failure.
   */
  virtual bool tune_options(int8_t opts) {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ != 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "already opened");
      return false;
    }
    opts_ = opts;
    return true;
  }
  /**
   * Set the number of buckets of the hash table.
   * @param bnum the number of buckets of the hash table.
   * @return true on success, or false on failure.
   */
  virtual bool tune_buckets(int64_t bnum) {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ != 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "already opened");
      return false;
    }
    bnum_ = bnum > 0 ? bnum : HDBDEFBNUM;
    if (bnum_ > INT16_MAX) bnum_ = nearbyprime(bnum_);
    return true;
  }
  /**
   * Set the size of the internal memory-mapped region.
   * @param msiz the size of the internal memory-mapped region.
   * @return true on success, or false on failure.
   */
  virtual bool tune_map(int64_t msiz) {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ != 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "already opened");
      return false;
    }
    msiz_ = msiz >= 0 ? msiz : HDBDEFMSIZ;
    return true;
  }
  /**
   * Set the unit step number of auto defragmentation.
   * @param dfunit the unit step number of auto defragmentation.
   * @return true on success, or false on failure.
   */
  virtual bool tune_defrag(int64_t dfunit) {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ != 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "already opened");
      return false;
    }
    dfunit_ = dfunit > 0 ? dfunit : 0;
    return true;
  }
  /**
   * Set the data compressor.
   * @param comp the data compressor object.
   * @return true on success, or false on failure.
   */
  virtual bool tune_compressor(Compressor* comp) {
    _assert_(comp);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ != 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "already opened");
      return false;
    }
    embcomp_ = comp;
    return true;
  }
  /**
   * Get the opaque data.
   * @return the pointer to the opaque data region, whose size is 16 bytes.
   */
  virtual char* opaque() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return NULL;
    }
    return opaque_;
  }
  /**
   * Synchronize the opaque data.
   * @return true on success, or false on failure.
   */
  virtual bool synchronize_opaque() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return false;
    }
    bool err = false;
    if (!dump_opaque()) err = true;
    return !err;
  }
  /**
   * Perform defragmentation of the file.
   * @param step the number of steps.  If it is not more than 0, the whole region is defraged.
   * @return true on success, or false on failure.
   */
  virtual bool defrag(int64_t step) {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return false;
    }
    if (!writer_) {
      set_error(__FILE__, __LINE__, Error::NOPERM, "permission denied");
      return false;
    }
    bool err = false;
    if (step > 0) {
      if (!defrag_impl(step)) err = true;
    } else {
      dfcur_ = roff_;
      if (!defrag_impl(INT64_MAX)) err = true;
    }
    frgcnt_ = 0;
    return !err;
  }
  /**
   * Get the status flags.
   * @return the status flags, or 0 on failure.
   */
  virtual uint8_t flags() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return flags_;
  }
protected:
  /**
   * Set the error information.
   * @param file the file name of the epicenter.
   * @param line the line number of the epicenter.
   * @param code an error code.
   * @param message a supplement message.
   */
  virtual void set_error(const char* file, int32_t line,
                         Error::Code code, const char* message) {
    _assert_(file && message);
    set_error(code, message);
    if (ervbs_ || code == Error::BROKEN || code == Error::SYSTEM)
      report(file, line, "error", "%d: %s: %s", code, Error::codename(code), message);
  }
  /**
   * Report a message for debugging.
   * @param file the file name of the epicenter.
   * @param line the line number of the epicenter.
   * @param type the type string.
   * @param format the printf-like format string.
   * @param ... used according to the format string.
   */
  virtual void report(const char* file, int32_t line, const char* type,
                      const char* format, ...) {
    _assert_(file && line > 0 && type && format);
    if (!erstrm_) return;
    const std::string& path = path_.size() > 0 ? path_ : "-";
    std::string message;
    va_list ap;
    va_start(ap, format);
    strprintf(&message, format, ap);
    va_end(ap);
    *erstrm_ << "[" << type << "]: " << path << ": " << file << ": " << line;
    *erstrm_ << ": " << message << std::endl;
  }
  /**
   * Report the content of a binary buffer for debugging.
   * @param file the file name of the epicenter.
   * @param line the line number of the epicenter.
   * @param type the type string.
   * @param name the name of the information.
   * @param buf the binary buffer.
   * @param size the size of the binary buffer
   */
  virtual void report_binary(const char* file, int32_t line, const char* type,
                             const char* name, const char* buf, size_t size) {
    _assert_(file && line > 0 && type && name && buf && size <= MEMMAXSIZ);
    if (!erstrm_) return;
    char* hex = hexencode(buf, size);
    report(file, line, type, "%s=%s", name, hex);
    delete[] hex;
  }
  /**
   * Set the database type.
   * @param type the database type.
   * @return true on success, or false on failure.
   */
  virtual bool tune_type(int8_t type) {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, true);
    if (omode_ != 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "already opened");
      return false;
    }
    type_ = type;
    return true;
  }
  /**
   * Get the library version.
   * @return the library version, or 0 on failure.
   */
  virtual uint8_t libver() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return libver_;
  }
  /**
   * Get the library revision.
   * @return the library revision, or 0 on failure.
   */
  virtual uint8_t librev() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return librev_;
  }
  /**
   * Get the format version.
   * @return the format version, or 0 on failure.
   */
  virtual uint8_t fmtver() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return fmtver_;
  }
  /**
   * Get the module checksum.
   * @return the module checksum, or 0 on failure.
   */
  virtual uint8_t chksum() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return chksum_;
  }
  /**
   * Get the database type.
   * @return the database type, or 0 on failure.
   */
  virtual uint8_t type() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return type_;
  }
  /**
   * Get the alignment power.
   * @return the alignment power, or 0 on failure.
   */
  virtual uint8_t apow() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return apow_;
  }
  /**
   * Get the free block pool power.
   * @return the free block pool power, or 0 on failure.
   */
  virtual uint8_t fpow() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return fpow_;
  }
  /**
   * Get the options.
   * @return the options, or 0 on failure.
   */
  virtual uint8_t opts() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return opts_;
  }
  /**
   * Get the bucket number.
   * @return the bucket number, or 0 on failure.
   */
  virtual int64_t bnum() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return bnum_;
  }
  /**
   * Get the size of the internal memory-mapped region.
   * @return the size of the internal memory-mapped region, or 0 on failure.
   */
  virtual int64_t msiz() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return msiz_;
  }
  /**
   * Get the unit step number of auto defragmentation.
   * @return the unit step number of auto defragmentation, or 0 on failure.
   */
  virtual int64_t dfunit() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return 0;
    }
    return dfunit_;
  }
  /**
   * Get the data compressor.
   * @return the data compressor, or NULL on failure.
   */
  virtual Compressor *comp() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return NULL;
    }
    return comp_;
  }
  /**
   * Check whether the database was recovered or not.
   * @return true if recovered, or false if not.
   */
  virtual bool recovered() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return false;
    }
    return file_.recovered();
  }
  /**
   * Check whether the database was reorganized or not.
   * @return true if recovered, or false if not.
   */
  virtual bool reorganized() {
    _assert_(true);
    ScopedSpinRWLock lock(&mlock_, false);
    if (omode_ == 0) {
      set_error(__FILE__, __LINE__, Error::INVALID, "not opened");
      return false;
    }
    return reorg_;
  }
private:
  /**
   * Record data.
   */
  struct Record {
    int64_t off;                         ///< offset
    size_t rsiz;                         ///< whole size
    size_t psiz;                         ///< size of the padding
    size_t ksiz;                         ///< size of the key
    size_t vsiz;                         ///< size of the value
    int64_t left;                        ///< address of the left child record
    int64_t right;                       ///< address of the right child record
    const char* kbuf;                    ///< pointer to the key
    const char* vbuf;                    ///< pointer to the value
    int64_t boff;                        ///< offset of the body
    char* bbuf;                          ///< buffer of the body
  };
  /**
   * Free block data.
   */
  struct FreeBlock {
    int64_t off;                         ///< offset
    size_t rsiz;                         ///< record size
    /** comparing operator */
    bool operator <(const FreeBlock& obj) const {
      _assert_(true);
      if (rsiz < obj.rsiz) return true;
      if (rsiz == obj.rsiz && off > obj.off) return true;
      return false;
    }
  };
  /**
   * Comparator for free blocks.
   */
  struct FreeBlockComparator {
    /** comparing operator */
    bool operator ()(const FreeBlock& a, const FreeBlock& b) const {
      _assert_(true);
      return a.off < b.off;
    }
  };
  /**
   * Repeating visitor.
   */
  class Repeater : public Visitor {
  public:
    explicit Repeater(const char* vbuf, size_t vsiz) : vbuf_(vbuf), vsiz_(vsiz) {
      _assert_(vbuf);
    }
  private:
    const char* visit_full(const char* kbuf, size_t ksiz,
                           const char* vbuf, size_t vsiz, size_t* sp) {
      _assert_(kbuf && vbuf && sp);
      *sp = vsiz_;
      return vbuf_;
    }
    const char* vbuf_;
    size_t vsiz_;
  };
  /** Dummy constructor to forbid the use. */
  HashDB(const HashDB&);
  /** Dummy Operator to forbid the use. */
  HashDB& operator =(const HashDB&);
  /**
   * Accept a visitor to a record.
   * @param kbuf the pointer to the key region.
   * @param ksiz the size of the key region.
   * @param visitor a visitor object.
   * @param bidx the bucket index.
   * @param pivot the second hash value.
   @ @param isiter true for iterator use, or false for direct use.
   * @return true on success, or false on failure.
   */
  bool accept_impl(const char* kbuf, size_t ksiz, Visitor* visitor,
                   int64_t bidx, uint32_t pivot, bool isiter) {
    _assert_(kbuf && ksiz <= MEMMAXSIZ && visitor && bidx >= 0);
    int64_t off = get_bucket(bidx);
    if (off < 0) return false;
    int64_t entoff = 0;
    Record rec;
    char rbuf[HDBRECBUFSIZ];
    while (off > 0) {
      rec.off = off;
      if (!read_record(&rec, rbuf)) return false;
      if (rec.psiz == UINT16_MAX) {
        set_error(__FILE__, __LINE__, Error::BROKEN, "free block in the chain");
        report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
               (long)psiz_, (long)rec.off, (long)file_.size());
        return false;
      }
      uint32_t tpivot = linear_ ? pivot : fold_hash(hash_record(rec.kbuf, rec.ksiz));
      if (pivot > tpivot) {
        delete[] rec.bbuf;
        off = rec.left;
        entoff = rec.off + sizeof(uint16_t);
      } else if (pivot < tpivot) {
        delete[] rec.bbuf;
        off = rec.right;
        entoff = rec.off + sizeof(uint16_t) + width_;
      } else {
        int32_t kcmp = compare_keys(kbuf, ksiz, rec.kbuf, rec.ksiz);
        if (linear_ && kcmp != 0) kcmp = 1;
        if (kcmp > 0) {
          delete[] rec.bbuf;
          off = rec.left;
          entoff = rec.off + sizeof(uint16_t);
        } else if (kcmp < 0) {
          delete[] rec.bbuf;
          off = rec.right;
          entoff = rec.off + sizeof(uint16_t) + width_;
        } else {
          if (!rec.vbuf && !read_record_body(&rec)) {
            delete[] rec.bbuf;
            return false;
          }
          const char* vbuf = rec.vbuf;
          size_t vsiz = rec.vsiz;
          char* zbuf = NULL;
          size_t zsiz = 0;
          if (comp_) {
            zbuf = comp_->decompress(vbuf, vsiz, &zsiz);
            if (!zbuf) {
              set_error(__FILE__, __LINE__, Error::SYSTEM, "data decompression failed");
              delete[] rec.bbuf;
              return false;
            }
            vbuf = zbuf;
            vsiz = zsiz;
          }
          vbuf = visitor->visit_full(kbuf, ksiz, vbuf, vsiz, &vsiz);
          delete[] zbuf;
          if (vbuf == Visitor::REMOVE) {
            bool atran = false;
            if (autotran_ && !tran_) {
              if (!begin_auto_transaction()) {
                delete[] rec.bbuf;
                return false;
              }
              atran = true;
            }
            if (!write_free_block(rec.off, rec.rsiz, rbuf)) {
              if (atran) abort_auto_transaction();
              delete[] rec.bbuf;
              return false;
            }
            insert_free_block(rec.off, rec.rsiz);
            frgcnt_ += 1;
            delete[] rec.bbuf;
            if (!cut_chain(&rec, rbuf, bidx, entoff)) {
              if (atran) abort_auto_transaction();
              return false;
            }
            count_ -= 1;
            if (atran) {
              if (!commit_auto_transaction()) return false;
            } else if (autosync_) {
              if (!synchronize_meta()) return false;
            }
          } else if (vbuf == Visitor::NOP) {
            delete[] rec.bbuf;
          } else {
            zbuf = NULL;
            zsiz = 0;
            if (comp_ && !isiter) {
              zbuf = comp_->compress(vbuf, vsiz, &zsiz);
              if (!zbuf) {
                set_error(__FILE__, __LINE__, Error::SYSTEM, "data compression failed");
                delete[] rec.bbuf;
                return false;
              }
              vbuf = zbuf;
              vsiz = zsiz;
            }
            bool atran = false;
            if (autotran_ && !tran_) {
              if (!begin_auto_transaction()) {
                delete[] zbuf;
                delete[] rec.bbuf;
                return false;
              }
              atran = true;
            }
            size_t rsiz = calc_record_size(rec.ksiz, vsiz);
            if (rsiz <= rec.rsiz) {
              rec.psiz = rec.rsiz - rsiz;
              rec.vsiz = vsiz;
              rec.vbuf = vbuf;
              if (!adjust_record(&rec) || !write_record(&rec, true)) {
                if (atran) abort_auto_transaction();
                delete[] zbuf;
                delete[] rec.bbuf;
                return false;
              }
              delete[] zbuf;
              delete[] rec.bbuf;
            } else {
              if (!write_free_block(rec.off, rec.rsiz, rbuf)) {
                if (atran) abort_auto_transaction();
                delete[] zbuf;
                delete[] rec.bbuf;
                return false;
              }
              insert_free_block(rec.off, rec.rsiz);
              frgcnt_ += 1;
              size_t psiz = calc_record_padding(rsiz);
              rec.rsiz = rsiz + psiz;
              rec.psiz = psiz;
              rec.vsiz = vsiz;
              rec.vbuf = vbuf;
              bool over = false;
              FreeBlock fb;
              if (!isiter && fetch_free_block(rec.rsiz, &fb)) {
                rec.off = fb.off;
                rec.rsiz = fb.rsiz;
                rec.psiz = rec.rsiz - rsiz;
                over = true;
                if (!adjust_record(&rec)) {
                  if (atran) abort_auto_transaction();
                  delete[] zbuf;
                  delete[] rec.bbuf;
                  return false;
                }
              } else {
                rec.off = lsiz_.add(rec.rsiz);
              }
              if (!write_record(&rec, over)) {
                if (atran) abort_auto_transaction();
                delete[] zbuf;
                delete[] rec.bbuf;
                return false;
              }
              if (!over) psiz_.secure_least(rec.off + rec.rsiz);
              delete[] zbuf;
              delete[] rec.bbuf;
              if (entoff > 0) {
                if (!set_chain(entoff, rec.off)) {
                  if (atran) abort_auto_transaction();
                  return false;
                }
              } else {
                if (!set_bucket(bidx, rec.off)) {
                  if (atran) abort_auto_transaction();
                  return false;
                }
              }
            }
            if (atran) {
              if (!commit_auto_transaction()) return false;
            } else if (autosync_) {
              if (!synchronize_meta()) return false;
            }
          }
          return true;
        }
      }
    }
    size_t vsiz;
    const char* vbuf = visitor->visit_empty(kbuf, ksiz, &vsiz);
    if (vbuf != Visitor::NOP && vbuf != Visitor::REMOVE) {
      char* zbuf = NULL;
      size_t zsiz = 0;
      if (comp_) {
        zbuf = comp_->compress(vbuf, vsiz, &zsiz);
        if (!zbuf) {
          set_error(__FILE__, __LINE__, Error::SYSTEM, "data compression failed");
          return false;
        }
        vbuf = zbuf;
        vsiz = zsiz;
      }
      bool atran = false;
      if (autotran_ && !tran_) {
        if (!begin_auto_transaction()) {
          delete[] zbuf;
          return false;
        }
        atran = true;
      }
      size_t rsiz = calc_record_size(ksiz, vsiz);
      size_t psiz = calc_record_padding(rsiz);
      rec.rsiz = rsiz + psiz;
      rec.psiz = psiz;
      rec.ksiz = ksiz;
      rec.vsiz = vsiz;
      rec.left = 0;
      rec.right = 0;
      rec.kbuf = kbuf;
      rec.vbuf = vbuf;
      bool over = false;
      FreeBlock fb;
      if (fetch_free_block(rec.rsiz, &fb)) {
        rec.off = fb.off;
        rec.rsiz = fb.rsiz;
        rec.psiz = rec.rsiz - rsiz;
        over = true;
        if (!adjust_record(&rec)) {
          if (atran) abort_auto_transaction();
          delete[] zbuf;
          return false;
        }
      } else {
        rec.off = lsiz_.add(rec.rsiz);
      }
      if (!write_record(&rec, over)) {
        if (atran) abort_auto_transaction();
        delete[] zbuf;
        return false;
      }
      if (!over) psiz_.secure_least(rec.off + rec.rsiz);
      delete[] zbuf;
      if (entoff > 0) {
        if (!set_chain(entoff, rec.off)) {
          if (atran) abort_auto_transaction();
          return false;
        }
      } else {
        if (!set_bucket(bidx, rec.off)) {
          if (atran) abort_auto_transaction();
          return false;
        }
      }
      count_ += 1;
      if (atran) {
        if (!commit_auto_transaction()) return false;
      } else if (autosync_) {
        if (!synchronize_meta()) return false;
      }
    }
    return true;
  }
  /**
   * Iterate to accept a visitor for each record.
   * @param visitor a visitor object.
   * @return true on success, or false on failure.
   */
  bool iterate_impl(Visitor* visitor) {
    _assert_(visitor);
    int64_t off = roff_;
    int64_t end = lsiz_;
    Record rec;
    char rbuf[HDBRECBUFSIZ];
    while (off > 0 && off < end) {
      rec.off = off;
      if (!read_record(&rec, rbuf)) return false;
      if (rec.psiz == UINT16_MAX) {
        off += rec.rsiz;
      } else {
        if (!rec.vbuf && !read_record_body(&rec)) {
          delete[] rec.bbuf;
          return false;
        }
        const char* vbuf = rec.vbuf;
        size_t vsiz = rec.vsiz;
        char* zbuf = NULL;
        size_t zsiz = 0;
        if (comp_) {
          zbuf = comp_->decompress(vbuf, vsiz, &zsiz);
          if (!zbuf) {
            set_error(__FILE__, __LINE__, Error::SYSTEM, "data decompression failed");
            delete[] rec.bbuf;
            return false;
          }
          vbuf = zbuf;
          vsiz = zsiz;
        }
        vbuf = visitor->visit_full(rec.kbuf, rec.ksiz, vbuf, vsiz, &vsiz);
        delete[] zbuf;
        if (vbuf == Visitor::REMOVE) {
          uint64_t hash = hash_record(rec.kbuf, rec.ksiz);
          uint32_t pivot = fold_hash(hash);
          int64_t bidx = hash % bnum_;
          Repeater repeater(Visitor::REMOVE, 0);
          if (!accept_impl(rec.kbuf, rec.ksiz, &repeater, bidx, pivot, true)) {
            delete[] rec.bbuf;
            return false;
          }
          delete[] rec.bbuf;
        } else if (vbuf == Visitor::NOP) {
          delete[] rec.bbuf;
        } else {
          zbuf = NULL;
          zsiz = 0;
          if (comp_) {
            zbuf = comp_->compress(vbuf, vsiz, &zsiz);
            if (!zbuf) {
              set_error(__FILE__, __LINE__, Error::SYSTEM, "data compression failed");
              delete[] rec.bbuf;
              return false;
            }
            vbuf = zbuf;
            vsiz = zsiz;
          }
          size_t rsiz = calc_record_size(rec.ksiz, vsiz);
          if (rsiz <= rec.rsiz) {
            rec.psiz = rec.rsiz - rsiz;
            rec.vsiz = vsiz;
            rec.vbuf = vbuf;
            if (!adjust_record(&rec) || !write_record(&rec, true)) {
              delete[] zbuf;
              delete[] rec.bbuf;
              return false;
            }
            delete[] zbuf;
            delete[] rec.bbuf;
          } else {
            uint64_t hash = hash_record(rec.kbuf, rec.ksiz);
            uint32_t pivot = fold_hash(hash);
            int64_t bidx = hash % bnum_;
            Repeater repeater(vbuf, vsiz);
            if (!accept_impl(rec.kbuf, rec.ksiz, &repeater, bidx, pivot, true)) {
              delete[] zbuf;
              delete[] rec.bbuf;
              return false;
            }
            delete[] zbuf;
            delete[] rec.bbuf;
          }
        }
        off += rec.rsiz;
      }
    }
    return true;
  }
  /**
   * Synchronize updated contents with the file and the device.
   * @param hard true for physical synchronization with the device, or false for logical
   * synchronization with the file system.
   * @param proc a postprocessor object.
   * @return true on success, or false on failure.
   */
  bool synchronize_impl(bool hard, FileProcessor* proc) {
    _assert_(true);
    bool err = false;
    if (hard && !dump_free_blocks()) err = true;
    if (!dump_meta()) err = true;
    if (!file_.synchronize(hard)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      err = true;
    }
    if (proc && !proc->process(path_, count_, lsiz_)) {
      set_error(__FILE__, __LINE__, Error::LOGIC, "postprocessing failed");
      err = true;
    }
    return !err;
  }
  /**
   * Synchronize meta data with the file and the device.
   * @return true on success, or false on failure.
   */
  bool synchronize_meta() {
    _assert_(true);
    ScopedSpinLock lock(&flock_);
    bool err = false;
    if (!dump_meta()) err = true;
    if (!file_.synchronize(true)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      err = true;
    }
    return !err;
  }
  /**
   * Perform defragmentation.
   * @param step the number of steps.
   * @return true on success, or false on failure.
   */
  bool defrag_impl(int64_t step) {
    _assert_(step >= 0);
    int64_t end = lsiz_;
    Record rec;
    char rbuf[HDBRECBUFSIZ];
    while (true) {
      if (dfcur_ >= end) {
        dfcur_ = roff_;
        return true;
      }
      if (step-- < 1) return true;
      rec.off = dfcur_;
      if (!read_record(&rec, rbuf)) return false;
      if (rec.psiz == UINT16_MAX) break;
      delete[] rec.bbuf;
      dfcur_ += rec.rsiz;
    }
    int64_t base = dfcur_;
    int64_t dest = base;
    dfcur_ += rec.rsiz;
    step++;
    while (step-- > 0 && dfcur_ < end) {
      rec.off = dfcur_;
      if (!read_record(&rec, rbuf)) return false;
      escape_cursors(rec.off, dest);
      dfcur_ += rec.rsiz;
      if (rec.psiz != UINT16_MAX) {
        if (!rec.vbuf && !read_record_body(&rec)) {
          delete[] rec.bbuf;
          return false;
        }
        if (rec.psiz >= align_) {
          size_t diff = rec.psiz - rec.psiz % align_;
          rec.psiz -= diff;
          rec.rsiz -= diff;
        }
        if (!shift_record(&rec, dest)) {
          delete[] rec.bbuf;
          return false;
        }
        delete[] rec.bbuf;
        dest += rec.rsiz;
      }
    }
    trim_free_blocks(base, dfcur_);
    if (dfcur_ >= end) {
      lsiz_ = dest;
      psiz_ = lsiz_;
      if (!file_.truncate(lsiz_)) return false;
      trim_cursors();
      dfcur_ = roff_;
    } else {
      size_t rsiz = dfcur_ - dest;
      if (!write_free_block(dest, rsiz, rbuf)) return false;
      insert_free_block(dest, rsiz);
      dfcur_ = dest;
    }
    return true;
  }
  /**
   * Calculate meta data with saved ones.
   */
  void calc_meta() {
    _assert_(true);
    align_ = 1 << apow_;
    fbpnum_ = fpow_ > 0 ? 1 << fpow_ : 0;
    width_ = (opts_ & TSMALL) ? HDBWIDTHSMALL : HDBWIDTHLARGE;
    linear_ = (opts_ & TLINEAR) ? true : false;
    comp_ = (opts_ & TCOMPRESS) ? embcomp_ : NULL;
    rhsiz_ = sizeof(uint16_t) + sizeof(uint8_t) * 2;
    rhsiz_ += linear_ ? width_ : width_ * 2;
    boff_ = HDBHEADSIZ + HDBFBPWIDTH * fbpnum_;
    if (fbpnum_ > 0) boff_ += width_ * 2 + sizeof(uint8_t) * 2;
    roff_ = boff_ + width_ * bnum_;
    int64_t rem = roff_ % align_;
    if (rem > 0) roff_ += align_ - rem;
    dfcur_ = roff_;
    frgcnt_ = 0;
    tran_ = false;
  }
  /**
   * Calculate the module checksum.
   */
  uint8_t calc_checksum() {
    _assert_(true);
    const char* kbuf = HDBCHKSUMSEED;
    size_t ksiz = sizeof(HDBCHKSUMSEED) - 1;
    char* zbuf = NULL;
    size_t zsiz = 0;
    if (comp_) {
      zbuf = comp_->compress(kbuf, ksiz, &zsiz);
      if (!zbuf) return 0;
      kbuf = zbuf;
      ksiz = zsiz;
    }
    uint32_t hash = fold_hash(hash_record(kbuf, ksiz));
    delete[] zbuf;
    return (hash >> 24) ^ (hash >> 16) ^ (hash >> 8) ^ (hash >> 0);
  }
  /**
   * Dump the meta data into the file.
   * @return true on success, or false on failure.
   */
  bool dump_meta() {
    _assert_(true);
    char head[HDBHEADSIZ];
    std::memset(head, 0, sizeof(head));
    std::memcpy(head, HDBMAGICDATA, sizeof(HDBMAGICDATA));
    std::memcpy(head + HDBMOFFLIBVER, &libver_, sizeof(libver_));
    std::memcpy(head + HDBMOFFLIBREV, &librev_, sizeof(librev_));
    std::memcpy(head + HDBMOFFFMTVER, &fmtver_, sizeof(fmtver_));
    std::memcpy(head + HDBMOFFCHKSUM, &chksum_, sizeof(chksum_));
    std::memcpy(head + HDBMOFFTYPE, &type_, sizeof(type_));
    std::memcpy(head + HDBMOFFAPOW, &apow_, sizeof(apow_));
    std::memcpy(head + HDBMOFFFPOW, &fpow_, sizeof(fpow_));
    std::memcpy(head + HDBMOFFOPTS, &opts_, sizeof(opts_));
    uint64_t num = hton64(bnum_);
    std::memcpy(head + HDBMOFFBNUM, &num, sizeof(num));
    uint8_t flags = flags_;
    if (!flagopen_) flags &= ~FOPEN;
    std::memcpy(head + HDBMOFFFLAGS, &flags, sizeof(flags));
    num = hton64(count_);
    std::memcpy(head + HDBMOFFCOUNT, &num, sizeof(num));
    num = hton64(lsiz_);
    std::memcpy(head + HDBMOFFSIZE, &num, sizeof(num));
    std::memcpy(head + HDBMOFFOPAQUE, opaque_, sizeof(opaque_));
    if (!file_.write(0, head, sizeof(head))) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      return false;
    }
    return true;
  }
  /**
   * Dump the meta data into the file.
   * @return true on success, or false on failure.
   */
  bool dump_auto_meta() {
    _assert_(true);
    const int64_t hsiz = HDBMOFFOPAQUE - HDBMOFFCOUNT;
    char head[hsiz];
    std::memset(head, 0, hsiz);
    uint64_t num = hton64(count_);
    std::memcpy(head, &num, sizeof(num));
    num = hton64(lsiz_);
    std::memcpy(head + HDBMOFFSIZE - HDBMOFFCOUNT, &num, sizeof(num));
    if (!file_.write_fast(HDBMOFFCOUNT, head, sizeof(head))) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      return false;
    }
    return true;
  }
  /**
   * Dump the opaque data into the file.
   * @return true on success, or false on failure.
   */
  bool dump_opaque() {
    _assert_(true);
    if (!file_.write_fast(HDBMOFFOPAQUE, opaque_, sizeof(opaque_))) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      return false;
    }
    return true;
  }
  /**
   * Load the meta data from the file.
   * @return true on success, or false on failure.
   */
  bool load_meta() {
    _assert_(true);
    char head[HDBHEADSIZ];
    if (file_.size() < (int64_t)sizeof(head)) {
      set_error(__FILE__, __LINE__, Error::INVALID, "missing magic data of the file");
      return false;
    }
    if (!file_.read(0, head, sizeof(head))) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
             (long)psiz_, (long)0, (long)file_.size());
      return false;
    }
    if (std::memcmp(head, HDBMAGICDATA, sizeof(HDBMAGICDATA))) {
      set_error(__FILE__, __LINE__, Error::INVALID, "invalid magic data of the file");
      return false;
    }
    std::memcpy(&libver_, head + HDBMOFFLIBVER, sizeof(libver_));
    std::memcpy(&librev_, head + HDBMOFFLIBREV, sizeof(librev_));
    std::memcpy(&fmtver_, head + HDBMOFFFMTVER, sizeof(fmtver_));
    std::memcpy(&chksum_, head + HDBMOFFCHKSUM, sizeof(chksum_));
    std::memcpy(&type_, head + HDBMOFFTYPE, sizeof(type_));
    std::memcpy(&apow_, head + HDBMOFFAPOW, sizeof(apow_));
    std::memcpy(&fpow_, head + HDBMOFFFPOW, sizeof(fpow_));
    std::memcpy(&opts_, head + HDBMOFFOPTS, sizeof(opts_));
    uint64_t num;
    std::memcpy(&num, head + HDBMOFFBNUM, sizeof(num));
    bnum_ = ntoh64(num);
    std::memcpy(&flags_, head + HDBMOFFFLAGS, sizeof(flags_));
    flagopen_ = flags_ & FOPEN;
    std::memcpy(&num, head + HDBMOFFCOUNT, sizeof(num));
    count_ = ntoh64(num);
    std::memcpy(&num, head + HDBMOFFSIZE, sizeof(num));
    lsiz_ = ntoh64(num);
    psiz_ = lsiz_;
    std::memcpy(opaque_, head + HDBMOFFOPAQUE, sizeof(opaque_));
    return true;
  }
  /**
   * Set a status flag.
   * @param flag the flag kind.
   * @param sign whether to set or unset.
   * @return true on success, or false on failure.
   */
  bool set_flag(uint8_t flag, bool sign) {
    _assert_(true);
    uint8_t flags;
    if (!file_.read(HDBMOFFFLAGS, &flags, sizeof(flags))) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
             (long)psiz_, (long)HDBMOFFFLAGS, (long)file_.size());
      return false;
    }
    if (sign) {
      flags |= flag;
    } else {
      flags &= ~flag;
    }
    if (!file_.write(HDBMOFFFLAGS, &flags, sizeof(flags))) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      return false;
    }
    flags_ = flags;
    return true;
  }
  /**
   * Reorganize the whole file.
   * @param path the path of the database file.
   * @return true on success, or false on failure.
   */
  bool reorganize_file(const std::string& path) {
    _assert_(true);
    bool err = false;
    HashDB db;
    db.tune_type(type_);
    db.tune_alignment(apow_);
    db.tune_fbp(fpow_);
    db.tune_options(opts_);
    db.tune_buckets(bnum_);
    db.tune_map(msiz_);
    if (embcomp_) db.tune_compressor(embcomp_);
    const std::string& npath = path + File::EXTCHR + HDBTMPPATHEXT;
    if (db.open(npath, OWRITER | OCREATE | OTRUNCATE)) {
      report(__FILE__, __LINE__, "info", "reorganizing the database");
      lsiz_ = file_.size();
      psiz_ = lsiz_;
      if (copy_records(&db)) {
        if (db.close()) {
          File src;
          if (src.open(npath, File::OREADER | File::ONOLOCK, 0)) {
            File* dest = writer_ ? &file_ : new File();
            if (dest == &file_ || dest->open(path, File::OWRITER | File::ONOLOCK, 0)) {
              int64_t off = 0;
              int64_t size = src.size();
              char buf[HDBIOBUFSIZ*4];
              while (off < size) {
                int32_t psiz = size - off;
                if (psiz > (int32_t)sizeof(buf)) psiz = sizeof(buf);
                if (src.read(off, buf, psiz)) {
                  if (!dest->write(off, buf, psiz)) {
                    set_error(__FILE__, __LINE__, Error::SYSTEM, dest->error());
                    err = true;
                    break;
                  }
                } else {
                  set_error(__FILE__, __LINE__, Error::SYSTEM, src.error());
                  err = true;
                  break;
                }
                off += psiz;
              }
              if (!dest->truncate(size)) {
                set_error(__FILE__, __LINE__, Error::SYSTEM, dest->error());
                err = true;
              }
              if (dest != &file_) {
                if (!dest->close()) {
                  set_error(__FILE__, __LINE__, Error::SYSTEM, dest->error());
                  err = true;
                }
                if (!file_.refresh()) {
                  set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
                  err = true;
                }
              }
              if (!load_meta()) err = true;
              calc_meta();
              reorg_ = true;
              if (dest != &file_) delete dest;
            } else {
              set_error(__FILE__, __LINE__, Error::SYSTEM, dest->error());
              err = true;
            }
            src.close();
          } else {
            set_error(__FILE__, __LINE__, Error::SYSTEM, src.error());
            err = true;
          }
        } else {
          set_error(__FILE__, __LINE__, db.error().code(), "closing the destination failed");
          err = true;
        }
      } else {
        set_error(__FILE__, __LINE__, db.error().code(), "record copying failed");
        err = true;
      }
      File::remove(npath);
    } else {
      set_error(__FILE__, __LINE__, db.error().code(), "opening the destination failed");
      err = true;
    }
    return !err;
  }
  /**
   * Copy all records to another database.
   * @param dest the destination database.
   * @return true on success, or false on failure.
   */
  bool copy_records(HashDB* dest) {
    _assert_(dest);
    std::ostream* erstrm = erstrm_;
    erstrm_ = NULL;
    int64_t off = roff_;
    int64_t end = psiz_;
    Record rec;
    char rbuf[HDBRECBUFSIZ];
    while (off > 0 && off < end) {
      rec.off = off;
      if (!read_record(&rec, rbuf)) break;
      if (rec.psiz == UINT16_MAX) {
        off += rec.rsiz;
      } else {
        if (!rec.vbuf && !read_record_body(&rec)) {
          delete[] rec.bbuf;
          break;
        }
        const char* vbuf = rec.vbuf;
        size_t vsiz = rec.vsiz;
        char* zbuf = NULL;
        size_t zsiz = 0;
        if (comp_) {
          zbuf = comp_->decompress(vbuf, vsiz, &zsiz);
          if (!zbuf) {
            set_error(__FILE__, __LINE__, Error::SYSTEM, "data decompression failed");
            delete[] rec.bbuf;
            break;
          }
          vbuf = zbuf;
          vsiz = zsiz;
        }
        if (!dest->set(rec.kbuf, rec.ksiz, vbuf, vsiz)) {
          delete[] zbuf;
          delete[] rec.bbuf;
          break;
        }
        delete[] zbuf;
        delete[] rec.bbuf;
        off += rec.rsiz;
      }
    }
    erstrm_ = erstrm;
    return true;
  }
  /**
   * Trim the file size.
   * @param path the path of the database file.
   * @return true on success, or false on failure.
   */
  bool trim_file(const std::string& path) {
    _assert_(true);
    bool err = false;
    report(__FILE__, __LINE__, "info", "trimming the database");
    File* dest = writer_ ? &file_ : new File();
    if (dest == &file_ || dest->open(path, File::OWRITER | File::ONOLOCK, 0)) {
      if (!dest->truncate(lsiz_)) {
        set_error(__FILE__, __LINE__, Error::SYSTEM, dest->error());
        err = true;
      }
      if (dest != &file_) {
        if (!dest->close()) {
          set_error(__FILE__, __LINE__, Error::SYSTEM, dest->error());
          err = true;
        }
        if (!file_.refresh()) {
          set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
          err = true;
        }
      }
      trim_ = true;
    } else {
      set_error(__FILE__, __LINE__, Error::SYSTEM, dest->error());
      err = true;
    }
    if (dest != &file_) delete dest;
    return !err;
  }
  /**
   * Get the hash value of a record.
   * @param kbuf the pointer to the key region.
   * @param ksiz the size of the key region.
   * @return the hash value.
   */
  uint64_t hash_record(const char* kbuf, size_t ksiz) {
    _assert_(kbuf);
    return hashmurmur(kbuf, ksiz);
  }
  /**
   * Fold a hash value into a small number.
   * @param hash the hash number.
   * @return the result number.
   */
  uint32_t fold_hash(uint64_t hash) {
    _assert_(true);
    return (((hash & 0xffff000000000000ULL) >> 48) | ((hash & 0x0000ffff00000000ULL) >> 16)) ^
      (((hash & 0x000000000000ffffULL) << 16) | ((hash & 0x00000000ffff0000ULL) >> 16));
  }
  /**
   * Compare two keys in lexical order.
   * @param abuf one key.
   * @param asiz the size of the one key.
   * @param bbuf the other key.
   * @param bsiz the size of the other key.
   * @return positive if the former is big, or negative if the latter is big, or 0 if both are
   * equivalent.
   */
  int32_t compare_keys(const char* abuf, size_t asiz, const char* bbuf, size_t bsiz) {
    _assert_(abuf && bbuf);
    if (asiz != bsiz) return (int32_t)asiz - (int32_t)bsiz;
    return std::memcmp(abuf, bbuf, asiz);
  }
  /**
   * Set an address into a bucket.
   * @param bidx the index of the bucket.
   * @param off the address.
   * @return true on success, or false on failure.
   */
  bool set_bucket(int64_t bidx, int64_t off) {
    _assert_(bidx >= 0 && off >= 0);
    char buf[sizeof(uint64_t)];
    writefixnum(buf, off >> apow_, width_);
    if (!file_.write_fast(boff_ + bidx * width_, buf, width_)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      return false;
    }
    return true;
  }
  /**
   * Get an address from a bucket.
   * @param bidx the index of the bucket.
   * @return the address, or -1 on failure.
   */
  int64_t get_bucket(int64_t bidx) {
    _assert_(bidx >= 0);
    char buf[sizeof(uint64_t)];
    if (!file_.read_fast(boff_ + bidx * width_, buf, width_)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
             (long)psiz_, (long)boff_ + bidx * width_, (long)file_.size());
      return -1;
    }
    return readfixnum(buf, width_) << apow_;
  }
  /**
   * Set an address into a chain slot.
   * @param entoff the address of the chain slot.
   * @param off the destination address.
   * @return true on success, or false on failure.
   */
  bool set_chain(int64_t entoff, int64_t off) {
    _assert_(entoff >= 0 && off >= 0);
    char buf[sizeof(uint64_t)];
    writefixnum(buf, off >> apow_, width_);
    if (!file_.write_fast(entoff, buf, width_)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      return false;
    }
    return true;
  }
  /**
   * Read a record from the file.
   * @param rec the record structure.
   * @param rbuf the working buffer.
   * @return true on success, or false on failure.
   */
  bool read_record(Record* rec, char* rbuf) {
    _assert_(rec && rbuf);
    if (rec->off < roff_) {
      set_error(__FILE__, __LINE__, Error::BROKEN, "invalid record offset");
      report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
             (long)psiz_, (long)rec->off, (long)file_.size());
      return false;
    }
    size_t rsiz = psiz_ - rec->off;
    if (rsiz > HDBRECBUFSIZ) {
      rsiz = HDBRECBUFSIZ;
    } else {
      if (rsiz < rhsiz_) {
        set_error(__FILE__, __LINE__, Error::BROKEN, "too short record region");
        report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld rsiz=%ld fsiz=%ld",
               (long)psiz_, (long)rec->off, (long)rsiz, (long)file_.size());
        return false;
      }
      rsiz = rhsiz_;
    }
    if (!file_.read_fast(rec->off, rbuf, rsiz)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld rsiz=%ld fsiz=%ld",
             (long)psiz_, (long)rec->off, (long)rsiz, (long)file_.size());
      return false;
    }
    const char* rp = rbuf;
    uint16_t snum;
    if (*(uint8_t*)rp == HDBRECMAGIC) {
      ((uint8_t*)&snum)[0] = 0;
      ((uint8_t*)&snum)[1] = *(uint8_t*)(rp + 1);
    } else if (*(uint8_t*)rp >= 0x80) {
      if (*(uint8_t*)(rp++) != HDBFBMAGIC || *(uint8_t*)(rp++) != HDBFBMAGIC) {
        set_error(__FILE__, __LINE__, Error::BROKEN, "invalid magic data of a free block");
        report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld rsiz=%ld fsiz=%ld",
               (long)psiz_, (long)rec->off, (long)rsiz, (long)file_.size());
        report_binary(__FILE__, __LINE__, "info", "rbuf", rbuf, rsiz);
        return false;
      }
      rec->rsiz = readfixnum(rp, width_) << apow_;
      rp += width_;
      if (*(uint8_t*)(rp++) != HDBPADMAGIC || *(uint8_t*)(rp++) != HDBPADMAGIC) {
        set_error(__FILE__, __LINE__, Error::BROKEN, "invalid magic data of a free block");
        report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld rsiz=%ld fsiz=%ld",
               (long)psiz_, (long)rec->off, (long)rsiz, (long)file_.size());
        report_binary(__FILE__, __LINE__, "info", "rbuf", rbuf, rsiz);
        return false;
      }
      if (rec->rsiz < rhsiz_) {
        set_error(__FILE__, __LINE__, Error::BROKEN, "invalid size of a free block");
        report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld rsiz=%ld fsiz=%ld",
               (long)psiz_, (long)rec->off, (long)rsiz, (long)file_.size());
        report_binary(__FILE__, __LINE__, "info", "rbuf", rbuf, rsiz);
        return false;
      }
      rec->psiz = UINT16_MAX;
      rec->ksiz = 0;
      rec->vsiz = 0;
      rec->left = 0;
      rec->right = 0;
      rec->kbuf = NULL;
      rec->vbuf = NULL;
      rec->boff = 0;
      rec->bbuf = NULL;
      return true;
    } else if (*rp == 0) {
      set_error(__FILE__, __LINE__, Error::BROKEN, "nullified region");
      report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld rsiz=%ld fsiz=%ld",
             (long)psiz_, (long)rec->off, (long)rsiz, (long)file_.size());
      report_binary(__FILE__, __LINE__, "info", "rbuf", rbuf, rsiz);
      return false;
    } else {
      std::memcpy(&snum, rp, sizeof(snum));
    }
    rp += sizeof(snum);
    rsiz -= sizeof(snum);
    rec->psiz = ntoh16(snum);
    rec->left = readfixnum(rp, width_) << apow_;
    rp += width_;
    rsiz -= width_;
    if (linear_) {
      rec->right = 0;
    } else {
      rec->right = readfixnum(rp, width_) << apow_;
      rp += width_;
      rsiz -= width_;
    }
    uint64_t num;
    size_t step = readvarnum(rp, rsiz, &num);
    if (step < 1) {
      set_error(__FILE__, __LINE__, Error::BROKEN, "invalid key length");
      report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld rsiz=%ld fsiz=%ld snum=%04X",
             (long)psiz_, (long)rec->off, (long)rsiz, (long)file_.size(), snum);
      report_binary(__FILE__, __LINE__, "info", "rbuf", rbuf, rsiz);
      return false;
    }
    rec->ksiz = num;
    rp += step;
    rsiz -= step;
    step = readvarnum(rp, rsiz, &num);
    if (step < 1) {
      set_error(__FILE__, __LINE__, Error::BROKEN, "invalid value length");
      report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld rsiz=%ld fsiz=%ld snum=%04X",
             (long)psiz_, (long)rec->off, (long)rsiz, (long)file_.size(), snum);
      report_binary(__FILE__, __LINE__, "info", "rbuf", rbuf, rsiz);
      return false;
    }
    rec->vsiz = num;
    rp += step;
    rsiz -= step;
    size_t hsiz = rp - rbuf;
    rec->rsiz = hsiz + rec->ksiz + rec->vsiz + rec->psiz;
    rec->kbuf = NULL;
    rec->vbuf = NULL;
    rec->boff = rec->off + hsiz;
    rec->bbuf = NULL;
    if (rsiz >= rec->ksiz) {
      rec->kbuf = rp;
      rp += rec->ksiz;
      rsiz -= rec->ksiz;
      if (rsiz >= rec->vsiz) {
        rec->vbuf = rp;
        if (rec->psiz > 0) {
          rp += rec->vsiz;
          rsiz -= rec->vsiz;
          if (rsiz > 0 && *(uint8_t*)rp != HDBPADMAGIC) {
            set_error(__FILE__, __LINE__, Error::BROKEN, "invalid magic data of a record");
            report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld rsiz=%ld fsiz=%ld snum=%04X",
                   (long)psiz_, (long)rec->off, (long)rsiz, (long)file_.size(), snum);
            report_binary(__FILE__, __LINE__, "info", "rbuf", rbuf, rsiz);
            return false;
          }
        }
      }
    } else if (!read_record_body(rec)) {
      return false;
    }
    return true;
  }
  /**
   * Read the body of a record from the file.
   * @param rec the record structure.
   * @return true on success, or false on failure.
   */
  bool read_record_body(Record* rec) {
    _assert_(rec);
    size_t bsiz = rec->ksiz + rec->vsiz;
    char* bbuf = new char[bsiz];
    if (!file_.read_fast(rec->boff, bbuf, bsiz)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
             (long)psiz_, (long)rec->boff, (long)file_.size());
      delete[] bbuf;
      return false;
    }
    rec->bbuf = bbuf;
    rec->kbuf = rec->bbuf;
    rec->vbuf = rec->bbuf + rec->ksiz;
    return true;
  }
  /**
   * Write a record into the file.
   * @param rec the record structure.
   * @param over true for overwriting, or false for new record.
   * @return true on success, or false on failure.
   */
  bool write_record(Record* rec, bool over) {
    _assert_(rec);
    char stack[HDBIOBUFSIZ];
    char* rbuf = rec->rsiz > sizeof(stack) ? new char[rec->rsiz] : stack;
    char* wp = rbuf;
    uint16_t snum = hton16(rec->psiz);
    std::memcpy(wp, &snum, sizeof(snum));
    if (rec->psiz < 0x100) *wp = HDBRECMAGIC;
    wp += sizeof(snum);
    writefixnum(wp, rec->left >> apow_, width_);
    wp += width_;
    if (!linear_) {
      writefixnum(wp, rec->right >> apow_, width_);
      wp += width_;
    }
    wp += writevarnum(wp, rec->ksiz);
    wp += writevarnum(wp, rec->vsiz);
    std::memcpy(wp, rec->kbuf, rec->ksiz);
    wp += rec->ksiz;
    std::memcpy(wp, rec->vbuf, rec->vsiz);
    wp += rec->vsiz;
    if (rec->psiz > 0) {
      std::memset(wp, 0, rec->psiz);
      *wp = HDBPADMAGIC;
      wp += rec->psiz;
    }
    bool err = false;
    if (over) {
      if (!file_.write_fast(rec->off, rbuf, rec->rsiz)) {
        set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
        err = true;
      }
    } else {
      if (!file_.write(rec->off, rbuf, rec->rsiz)) {
        set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
        err = true;
      }
    }
    if (rbuf != stack) delete[] rbuf;
    return !err;
  }
  /**
   * Adjust the padding of a record.
   * @param rec the record structure.
   * @return true on success, or false on failure.
   */
  bool adjust_record(Record* rec) {
    _assert_(rec);
    if (rec->psiz > INT16_MAX || rec->psiz > rec->rsiz / 2) {
      size_t nsiz = (rec->psiz >> apow_) << apow_;
      if (nsiz < rhsiz_) return true;
      rec->rsiz -= nsiz;
      rec->psiz -= nsiz;
      int64_t noff = rec->off + rec->rsiz;
      char nbuf[HDBRECBUFSIZ];
      if (!write_free_block(noff, nsiz, nbuf)) return false;
      insert_free_block(noff, nsiz);
    }
    return true;
  }
  /**
   * Calculate the size of a record.
   * @param ksiz the size of the key.
   * @param vsiz the size of the value.
   * @return the size of the record.
   */
  size_t calc_record_size(size_t ksiz, size_t vsiz) {
    _assert_(true);
    size_t rsiz = sizeof(uint16_t) + width_;
    if (!linear_) rsiz += width_;
    if (ksiz < (1ULL << 7)) {
      rsiz += 1;
    } else if (ksiz < (1ULL << 14)) {
      rsiz += 2;
    } else if (ksiz < (1ULL << 21)) {
      rsiz += 3;
    } else if (ksiz < (1ULL << 28)) {
      rsiz += 4;
    } else {
      rsiz += 5;
    }
    if (vsiz < (1ULL << 7)) {
      rsiz += 1;
    } else if (vsiz < (1ULL << 14)) {
      rsiz += 2;
    } else if (vsiz < (1ULL << 21)) {
      rsiz += 3;
    } else if (vsiz < (1ULL << 28)) {
      rsiz += 4;
    } else {
      rsiz += 5;
    }
    rsiz += ksiz;
    rsiz += vsiz;
    return rsiz;
  }
  /**
   * Calculate the padding size of a record.
   * @param rsiz the size of the record.
   * @return the size of the padding.
   */
  size_t calc_record_padding(size_t rsiz) {
    _assert_(true);
    size_t diff = rsiz & (align_ - 1);
    return diff > 0 ? align_ - diff : 0;
  }
  /**
   * Shift a record to another place.
   * @param orec the original record structure.
   * @param dest the destination offset.
   * @return true on success, or false on failure.
   */
  bool shift_record(Record* orec, int64_t dest) {
    _assert_(orec && dest >= 0);
    uint64_t hash = hash_record(orec->kbuf, orec->ksiz);
    uint32_t pivot = fold_hash(hash);
    int64_t bidx = hash % bnum_;
    int64_t off = get_bucket(bidx);
    if (off < 0) return false;
    if (off == orec->off) {
      orec->off = dest;
      if (!write_record(orec, true)) return false;
      if (!set_bucket(bidx, dest)) return false;
      return true;
    }
    int64_t entoff = 0;
    Record rec;
    char rbuf[HDBRECBUFSIZ];
    while (off > 0) {
      rec.off = off;
      if (!read_record(&rec, rbuf)) return false;
      if (rec.psiz == UINT16_MAX) {
        set_error(__FILE__, __LINE__, Error::BROKEN, "free block in the chain");
        report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
               (long)psiz_, (long)rec.off, (long)file_.size());
        return false;
      }
      uint32_t tpivot = linear_ ? pivot : fold_hash(hash_record(rec.kbuf, rec.ksiz));
      if (pivot > tpivot) {
        delete[] rec.bbuf;
        off = rec.left;
        entoff = rec.off + sizeof(uint16_t);
      } else if (pivot < tpivot) {
        delete[] rec.bbuf;
        off = rec.right;
        entoff = rec.off + sizeof(uint16_t) + width_;
      } else {
        int32_t kcmp = compare_keys(orec->kbuf, orec->ksiz, rec.kbuf, rec.ksiz);
        if (linear_ && kcmp != 0) kcmp = 1;
        if (kcmp > 0) {
          delete[] rec.bbuf;
          off = rec.left;
          entoff = rec.off + sizeof(uint16_t);
        } else if (kcmp < 0) {
          delete[] rec.bbuf;
          off = rec.right;
          entoff = rec.off + sizeof(uint16_t) + width_;
        } else {
          delete[] rec.bbuf;
          orec->off = dest;
          if (!write_record(orec, true)) return false;
          if (entoff > 0) {
            if (!set_chain(entoff, dest)) return false;
          } else {
            if (!set_bucket(bidx, dest)) return false;
          }
          return true;
        }
      }
    }
    set_error(__FILE__, __LINE__, Error::BROKEN, "no record to shift");
    report(__FILE__, __LINE__, "info", "psiz=%ld fsiz=%ld", (long)psiz_, (long)file_.size());
    return false;
  }
  /**
   * Write a free block into the file.
   * @param off the offset of the free block.
   * @param rsiz the size of the free block.
   * @param rbuf the working buffer.
   * @return true on success, or false on failure.
   */
  bool write_free_block(int64_t off, size_t rsiz, char* rbuf) {
    _assert_(off >= 0 && rbuf);
    char* wp = rbuf;
    *(wp++) = HDBFBMAGIC;
    *(wp++) = HDBFBMAGIC;
    writefixnum(wp, rsiz >> apow_, width_);
    wp += width_;
    *(wp++) = HDBPADMAGIC;
    *(wp++) = HDBPADMAGIC;
    if (!file_.write_fast(off, rbuf, wp - rbuf)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      return false;
    }
    return true;
  }
  /**
   * Insert a free block to the free block pool.
   * @param off the offset of the free block.
   * @param rsiz the size of the free block.
   */
  void insert_free_block(int64_t off, size_t rsiz) {
    _assert_(off >= 0);
    ScopedSpinLock lock(&flock_);
    escape_cursors(off, off + rsiz);
    if (fbpnum_ < 1) return;
    if (fbp_.size() >= (size_t)fbpnum_) {
      FBP::const_iterator it = fbp_.begin();
      if (rsiz <= it->rsiz) return;
      fbp_.erase(it);
    }
    FreeBlock fb = { off, rsiz };
    fbp_.insert(fb);
  }
  /**
   * Fetch the free block pool from a decent sized block.
   * @param rsiz the minimum size of the block.
   * @param res the structure for the result.
   * @return true on success, or false on failure.
   */
  bool fetch_free_block(size_t rsiz, FreeBlock* res) {
    _assert_(res);
    if (fbpnum_ < 1) return false;
    ScopedSpinLock lock(&flock_);
    FreeBlock fb = { INT64_MAX, rsiz };
    FBP::const_iterator it = fbp_.upper_bound(fb);
    if (it == fbp_.end()) return false;
    res->off = it->off;
    res->rsiz = it->rsiz;
    fbp_.erase(it);
    escape_cursors(res->off, res->off + res->rsiz);
    return true;
  }
  /**
   * Trim invalid free blocks.
   * @param begin the beginning offset.
   * @param end the end offset.
   */
  void trim_free_blocks(int64_t begin, int64_t end) {
    _assert_(begin >= 0 && end >= 0);
    FBP::const_iterator it = fbp_.begin();
    FBP::const_iterator itend = fbp_.end();
    while (it != itend) {
      if (it->off >= begin && it->off < end) {
        fbp_.erase(it++);
      } else {
        it++;
      }
    }
  }
  /**
   * Dump all free blocks into the file.
   * @return true on success, or false on failure.
   */
  bool dump_free_blocks() {
    _assert_(true);
    if (fbpnum_ < 1) return true;
    size_t size = boff_ - HDBHEADSIZ;
    char* rbuf = new char[size];
    char* wp = rbuf;
    char* end = rbuf + size - width_ * 2 - sizeof(uint8_t) * 2;
    size_t num = fbp_.size();
    if (num > 0) {
      FreeBlock* blocks = new FreeBlock[num];
      size_t cnt = 0;
      FBP::const_iterator it = fbp_.begin();
      FBP::const_iterator itend = fbp_.end();
      while (it != itend) {
        blocks[cnt++] = *it;
        it++;
      }
      std::sort(blocks, blocks + num, FreeBlockComparator());
      for (size_t i = num - 1; i > 0; i--) {
        blocks[i].off -= blocks[i-1].off;
      }
      for (size_t i = 0; wp < end && i < num; i++) {
        wp += writevarnum(wp, blocks[i].off >> apow_);
        wp += writevarnum(wp, blocks[i].rsiz >> apow_);
      }
      delete[] blocks;
    }
    *(wp++) = 0;
    *(wp++) = 0;
    bool err = false;
    if (!file_.write(HDBHEADSIZ, rbuf, wp - rbuf)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      err = true;
    }
    delete[] rbuf;
    return !err;
  }
  /**
   * Dump an empty set of free blocks into the file.
   * @return true on success, or false on failure.
   */
  bool dump_empty_free_blocks() {
    _assert_(true);
    if (fbpnum_ < 1) return true;
    char rbuf[2];
    char* wp = rbuf;
    *(wp++) = 0;
    *(wp++) = 0;
    bool err = false;
    if (!file_.write(HDBHEADSIZ, rbuf, wp - rbuf)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      err = true;
    }
    return !err;
  }
  /**
   * Load all free blocks from from the file.
   * @return true on success, or false on failure.
   */
  bool load_free_blocks() {
    _assert_(true);
    if (fbpnum_ < 1) return true;
    size_t size = boff_ - HDBHEADSIZ;
    char* rbuf = new char[size];
    if (!file_.read(HDBHEADSIZ, rbuf, size)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
             (long)psiz_, (long)HDBHEADSIZ, (long)file_.size());
      delete[] rbuf;
      return false;
    }
    const char* rp = rbuf;
    FreeBlock* blocks = new FreeBlock[fbpnum_];
    int32_t num = 0;
    while (num < fbpnum_ && size > 1 && *rp != '\0') {
      uint64_t off;
      size_t step = readvarnum(rp, size, &off);
      if (step < 1 || off < 1) {
        set_error(__FILE__, __LINE__, Error::BROKEN, "invalid free block offset");
        report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
               (long)psiz_, (long)off, (long)file_.size());
        delete[] rbuf;
        delete[] blocks;
        return false;
      }
      rp += step;
      size -= step;
      uint64_t rsiz;
      step = readvarnum(rp, size, &rsiz);
      if (step < 1 || rsiz < 1) {
        set_error(__FILE__, __LINE__, Error::BROKEN, "invalid free block size");
        report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld rsiz=%ld fsiz=%ld",
               (long)psiz_, (long)off, (long)rsiz, (long)file_.size());
        delete[] rbuf;
        delete[] blocks;
        return false;
      }
      rp += step;
      size -= step;
      blocks[num].off = off << apow_;
      blocks[num].rsiz = rsiz << apow_;
      num++;
    }
    for (int32_t i = 1; i < num; i++) {
      blocks[i].off += blocks[i-1].off;
    }
    for (int32_t i = 0; i < num; i++) {
      FreeBlock fb = { blocks[i].off, blocks[i].rsiz };
      fbp_.insert(fb);
    }
    delete[] blocks;
    delete[] rbuf;
    return true;
  }
  /**
   * Disable all cursors.
   */
  void disable_cursors() {
    _assert_(true);
    if (curs_.size() < 1) return;
    CursorList::const_iterator cit = curs_.begin();
    CursorList::const_iterator citend = curs_.end();
    while (cit != citend) {
      Cursor* cur = *cit;
      cur->off_ = 0;
      cit++;
    }
  }
  /**
   * Escape cursors on a free block.
   * @param off the offset of the free block.
   * @param dest the destination offset.
   */
  void escape_cursors(int64_t off, int64_t dest) {
    _assert_(off >= 0 && dest >= 0);
    if (curs_.size() < 1) return;
    CursorList::const_iterator cit = curs_.begin();
    CursorList::const_iterator citend = curs_.end();
    while (cit != citend) {
      Cursor* cur = *cit;
      if (cur->end_ == off) {
        cur->end_ = dest;
        if (cur->off_ >= cur->end_) cur->off_ = 0;
      }
      if (cur->off_ == off) {
        cur->off_ = dest;
        if (cur->off_ >= cur->end_) cur->off_ = 0;
      }
      cit++;
    }
  }
  /**
   * Trim invalid cursors.
   */
  void trim_cursors() {
    _assert_(true);
    if (curs_.size() < 1) return;
    int64_t end = lsiz_;
    CursorList::const_iterator cit = curs_.begin();
    CursorList::const_iterator citend = curs_.end();
    while (cit != citend) {
      Cursor* cur = *cit;
      if (cur->off_ >= end) {
        cur->off_ = 0;
      } else if (cur->end_ > end) {
        cur->end_ = end;
      }
      cit++;
    }
  }
  /**
   * Remove a record from a bucket chain.
   * @param rec the record structure.
   * @param rbuf the working buffer.
   * @param bidx the bucket index.
   * @param entoff the offset of the entry pointer.
   * @return true on success, or false on failure.
   */
  bool cut_chain(Record* rec, char* rbuf, int64_t bidx, int64_t entoff) {
    _assert_(rec && rbuf && bidx >= 0 && entoff >= 0);
    int64_t child;
    if (rec->left > 0 && rec->right < 1) {
      child = rec->left;
    } else if (rec->left < 1 && rec->right > 0) {
      child = rec->right;
    } else if (rec->left < 1) {
      child = 0;
    } else {
      Record prec;
      prec.off = rec->left;
      if (!read_record(&prec, rbuf)) return false;
      if (prec.psiz == UINT16_MAX) {
        set_error(__FILE__, __LINE__, Error::BROKEN, "free block in the chain");
        report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
               (long)psiz_, (long)prec.off, (long)file_.size());
        report_binary(__FILE__, __LINE__, "info", "rbuf", rbuf, rhsiz_);
        return false;
      }
      delete[] prec.bbuf;
      if (prec.right > 0) {
        int64_t off = prec.right;
        int64_t pentoff = prec.off + sizeof(uint16_t) + width_;
        while (true) {
          prec.off = off;
          if (!read_record(&prec, rbuf)) return false;
          if (prec.psiz == UINT16_MAX) {
            set_error(__FILE__, __LINE__, Error::BROKEN, "free block in the chain");
            report(__FILE__, __LINE__, "info", "psiz=%ld off=%ld fsiz=%ld",
                   (long)psiz_, (long)prec.off, (long)file_.size());
            report_binary(__FILE__, __LINE__, "info", "rbuf", rbuf, rhsiz_);
            return false;
          }
          delete[] prec.bbuf;
          if (prec.right < 1) break;
          off = prec.right;
          pentoff = prec.off + sizeof(uint16_t) + width_;
        }
        child = off;
        if (!set_chain(pentoff, prec.left)) return false;
        if (!set_chain(off + sizeof(uint16_t), rec->left)) return false;
        if (!set_chain(off + sizeof(uint16_t) + width_, rec->right)) return false;
      } else {
        child = prec.off;
        if (!set_chain(prec.off + sizeof(uint16_t) + width_, rec->right)) return false;
      }
    }
    if (entoff > 0) {
      if (!set_chain(entoff, child)) return false;
    } else {
      if (!set_bucket(bidx, child)) return false;
    }
    return true;
  }
  /**
   * Begin transaction.
   * @return true on success, or false on failure.
   */
  bool begin_transaction_impl() {
    _assert_(true);
    if (!dump_meta()) return false;
    if (!file_.begin_transaction(trhard_, boff_)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      return false;
    }
    if (!file_.write_transaction(HDBMOFFBNUM, HDBHEADSIZ - HDBMOFFBNUM)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      file_.end_transaction(false);
      return false;
    }
    if (fbpnum_ > 0) {
      FBP::const_iterator it = fbp_.end();
      FBP::const_iterator itbeg = fbp_.begin();
      for (int32_t cnt = fpow_ * 2 + 1; cnt > 0; cnt--) {
        if (it == itbeg) break;
        it--;
        trfbp_.insert(*it);
      }
    }
    return true;
  }
  /**
   * Begin auto transaction.
   * @return true on success, or false on failure.
   */
  bool begin_auto_transaction() {
    _assert_(true);
    atlock_.lock();
    if (!file_.begin_transaction(autosync_, boff_)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      atlock_.unlock();
      return false;
    }
    if (!file_.write_transaction(HDBMOFFCOUNT, HDBMOFFOPAQUE - HDBMOFFCOUNT)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      file_.end_transaction(false);
      atlock_.unlock();
      return false;
    }
    return true;
  }
  /**
   * Commit transaction.
   * @return true on success, or false on failure.
   */
  bool commit_transaction() {
    _assert_(true);
    bool err = false;
    if (!dump_meta()) err = true;
    if (!file_.end_transaction(true)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      err = true;
    }
    trfbp_.clear();
    return !err;
  }
  /**
   * Commit auto transaction.
   * @return true on success, or false on failure.
   */
  bool commit_auto_transaction() {
    _assert_(true);
    bool err = false;
    if (!dump_auto_meta()) err = true;
    if (!file_.end_transaction(true)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      err = true;
    }
    atlock_.unlock();
    return !err;
  }
  /**
   * Abort transaction.
   * @return true on success, or false on failure.
   */
  bool abort_transaction() {
    _assert_(true);
    bool err = false;
    if (!file_.end_transaction(false)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      err = true;
    }
    if (!load_meta()) err = true;
    calc_meta();
    disable_cursors();
    fbp_.swap(trfbp_);
    trfbp_.clear();
    return !err;
  }
  /**
   * Abort auto transaction.
   * @return true on success, or false on failure.
   */
  bool abort_auto_transaction() {
    _assert_(true);
    bool err = false;
    if (!file_.end_transaction(false)) {
      set_error(__FILE__, __LINE__, Error::SYSTEM, file_.error());
      err = true;
    }
    if (!load_meta()) err = true;
    calc_meta();
    disable_cursors();
    fbp_.clear();
    atlock_.unlock();
    return !err;
  }
  /** The method lock. */
  SpinRWLock mlock_;
  /** The record locks. */
  SlottedSpinRWLock<HDBRLOCKSLOT> rlock_;
  /** The file lock. */
  SpinLock flock_;
  /** The auto transaction lock. */
  Mutex atlock_;
  /** The last happened error. */
  TSD<Error> error_;
  /** The internal error reporter. */
  std::ostream* erstrm_;
  /** The flag to report all errors. */
  bool ervbs_;
  /** The open mode. */
  uint32_t omode_;
  /** The flag for writer. */
  bool writer_;
  /** The flag for auto transaction. */
  bool autotran_;
  /** The flag for auto synchronization. */
  bool autosync_;
  /** The flag for reorganized. */
  bool reorg_;
  /** The flag for trimmed. */
  bool trim_;
  /** The file for data. */
  File file_;
  /** The free block pool. */
  FBP fbp_;
  /** The cursor objects. */
  CursorList curs_;
  /** The path of the database file. */
  std::string path_;
  /** The library version. */
  uint8_t libver_;
  /** The library revision. */
  uint8_t librev_;
  /** The format revision. */
  uint8_t fmtver_;
  /** The module checksum. */
  uint8_t chksum_;
  /** The database type. */
  uint8_t type_;
  /** The alignment power. */
  uint8_t apow_;
  /** The free block pool power. */
  uint8_t fpow_;
  /** The options. */
  uint8_t opts_;
  /** The bucket number. */
  int64_t bnum_;
  /** The status flags. */
  uint8_t flags_;
  /** The flag for open. */
  bool flagopen_;
  /** The record number. */
  AtomicInt64 count_;
  /** The logical size of the file. */
  AtomicInt64 lsiz_;
  /** The physical size of the file. */
  AtomicInt64 psiz_;
  /** The opaque data. */
  char opaque_[HDBHEADSIZ-HDBMOFFOPAQUE];
  /** The size of the internal memory-mapped region. */
  int64_t msiz_;
  /** The unit step number of auto defragmentation. */
  int64_t dfunit_;
  /** The embedded data compressor. */
  Compressor *embcomp_;
  /** The alignment of records. */
  size_t align_;
  /** The number of elements of the free block pool. */
  int32_t fbpnum_;
  /** The width of record addressing. */
  int32_t width_;
  /** The flag for linear collision chaining. */
  bool linear_;
  /** The data compressor. */
  Compressor* comp_;
  /** The header size of a record. */
  size_t rhsiz_;
  /** The offset of the buckets section. */
  int64_t boff_;
  /** The offset of the record section. */
  int64_t roff_;
  /** The defrag cursor. */
  int64_t dfcur_;
  /** The count of fragmentation. */
  AtomicInt64 frgcnt_;
  /** The flag whether in transaction. */
  bool tran_;
  /** The flag whether hard transaction. */
  bool trhard_;
  /** The escaped free block pool for transaction. */
  FBP trfbp_;
};


}                                        // common namespace

#endif                                   // duplication check

// END OF FILE
