Summary: Valgrind Memory Debugger
Name: valgrind
Version: 3.5.0
Release: 1
Epoch: 1
License: GPL
URL: http://www.valgrind.org/
Group: Development/Debuggers
Packager: Julian Seward <jseward@acm.org>
Source: valgrind-3.5.0.tar.bz2

Buildroot: %{_tmppath}/%{name}-root

%description 

Valgrind is an award-winning instrumentation framework for building dynamic
analysis tools. There are Valgrind tools that can automatically detect many
memory management and threading bugs, and profile your programs in detail. You
can also use Valgrind to build new tools.  Valgrind runs on the following
platforms: x86/Linux, AMD64/Linux, PPC32/Linux, PPC64/Linux, x86/MacOSX,
AMD64/MacOSX.

%prep
%setup -n valgrind-3.5.0

%build
%configure
make

%install
make install DESTDIR=$RPM_BUILD_ROOT

%makeinstall
mkdir docs.installed
mv $RPM_BUILD_ROOT%{_datadir}/doc/valgrind/* docs.installed/

%files
%defattr(-,root,root)
%doc AUTHORS COPYING FAQ.txt NEWS README*
%doc docs.installed/html/*.html docs.installed/html/images/*.png
%{_bindir}/*
%{_includedir}/valgrind
%{_libdir}/valgrind
%{_libdir}/pkgconfig/*

%doc
%defattr(-,root,root)
%{_mandir}/*/*

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf ${RPM_BUILD_ROOT}
