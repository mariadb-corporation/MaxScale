%define _topdir         %(echo $PWD)/
%define name            maxscale
%define release         ##RELEASE_TAG##
%define version         ##VERSION_TAG##
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
BuildRequires: gcc gcc-c++ ncurses-devel bison glibc-devel cmake libgcc perl make libtool openssl-devel libaio MariaDB-devel MariaDB-server

%description
MaxScale

%prep

%setup -q

%build
ln -s /lib64/libaio.so.1 /lib64/libaio.so
make ROOT_PATH=`pwd` HOME="" clean
make ROOT_PATH=`pwd` HOME="" depend
make ROOT_PATH=`pwd` HOME=""
make DEST=`pwd`/binaries ROOT_PATH=`pwd` HOME="" ERRMSG="/usr/share/mysql/english"  install

%post
ln -s /lib64/libaio.so.1 /lib64/libaio.so

%install
mkdir -p $RPM_BUILD_ROOT%{install_path}
cp -r binaries/* $RPM_BUILD_ROOT%{install_path}

%clean

%files
%defattr(-,root,root)
%{install_path}

%changelog
