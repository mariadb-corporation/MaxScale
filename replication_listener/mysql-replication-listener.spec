%define _libdir /usr/lib

Name:		mysql-replication-listener
Version:	0.0.47
Release:	10%{?dist}
Summary:	A STL/Boost based C++ library used for connecting to a MySQL server and process the replication stream as a slave.

Group:		Development/Libraries
License:	GNU GPL v2
URL:		https://bitbucket.org/winebarrel/mysql-replication-listener
#URL:		https://launchpad.net/mysql-replication-listener
Source0:	mysql-replication-listener.tar.gz
# git clone https://bitbucket.org/winebarrel/mysql-replication-listener.git
# cd mysql-replication-listener/
# git checkout refs/tags/0.0.47-10
# cd ..
# tar zcf mysql-replication-listener.tar.gz mysql-replication-listener/
BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

BuildRequires:	gcc-c++, make, cmake, boost-devel, openssl-devel
Requires:	glibc, libstdc++, zlib, boost-devel, openssl

%description
The MySQL Replicant Library is a C++ library for reading MySQL
replication events, either by connecting to a server or by reading
from a file. To handle reading from a server, it includes a very
simple client.

%prep
%setup -q -n %{name}

%build
%cmake
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_includedir}/access_method_factory.h
%{_includedir}/basic_content_handler.h
%{_includedir}/basic_transaction_parser.h
%{_includedir}/binlog_api.h
%{_includedir}/binlog_driver.h
%{_includedir}/binlog_event.h
%{_includedir}/bounded_buffer.h
%{_includedir}/field_iterator.h
%{_includedir}/file_driver.h
%{_includedir}/protocol.h
%{_includedir}/resultset_iterator.h
%{_includedir}/row_of_fields.h
%{_includedir}/rowset.h
%{_includedir}/tcp_driver.h
%{_includedir}/utilities.h
%{_includedir}/value.h
%{_libdir}/libreplication.a
%{_libdir}/libreplication.so
%{_libdir}/libreplication.so.0.1
%{_libdir}/libreplication.so.1
