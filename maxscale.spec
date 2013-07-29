%define _topdir         %(echo $PWD)/
%define name            maxscale
%define release         ##RELEASE_TAG##
%define version         ##VERSION_TAG##
%define buildroot	%{_topdir}/%{name}-%{version}-%{release}root
%define install_path    /usr/local/sbin/

BuildRoot:              %{buildroot}
Summary:                maxscale
License:                GPL
Name:                   %{name}
Version:                %{version}
Release:                %{release}
Source:                 %{name}-%{version}-%{release}.tar.gz
Prefix:                 /
Group:                  Development/Tools
#Requires:        
BuildRequires: gcc gcc-c++ ncurses-devel bison glibc-devel cmake libgcc perl make libtool openssl-devel

%description
galera-remote-exec

%prep

%setup -q

%build
make ROOT_PATH=`pwd` MARIADB_SRC_PATH=/home/skysql/mariadb-5.5.32 depend
make ROOT_PATH=`pwd` MARIADB_SRC_PATH=/home/skysql/mariadb-5.5.32
make DEST=`pwd`/binaries ROOT_PATH=`pwd` MARIADB_SRC_PATH=/home/skysql/mariadb-5.5.32 install

%post

%install
mkdir -p $RPM_BUILD_ROOT%{install_path}
cp -r binaries/* $RPM_BUILD_ROOT%{install_path}

%clean

%files
%defattr(-,root,root)
%{install_path}

%changelog

