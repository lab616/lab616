/*************************************************************************************************
 * Comparator functions
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


#include "kccompare.h"
#include "myconf.h"

namespace kyotocabinet {                 // common namespace


/**
 * Prepared variable of the comparator in the lexical order.
 */
LexicalComparator LEXICALCOMP;


/**
 * Prepared variable of the comparator in the decimal order.
 */
DecimalComparator DECIMALCOMP;


}                                        // common namespace

// END OF FILE
