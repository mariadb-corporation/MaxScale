%define _topdir         %(echo $PWD)/
%define name            maxscale
%define release         1
%define version         0.7
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
BuildRequires: gcc gcc-c++ ncurses-devel bison glibc-devel cmake libgcc perl make libtool openssl-devel libaio libaoi-devel MariaDB-devel MariaDB-server

%description
MaxScale

%prep

%setup -q

%build
#ln -s /lib64/libaio.so.1 /lib64/libaio.so
make ROOT_PATH=`pwd` HOME="" $DEBUG_FLAG1 $DEBUG_FLAG2 clean
make ROOT_PATH=`pwd` HOME="" $DEBUG_FLAG1 $DEBUG_FLAG2 depend
make ROOT_PATH=`pwd` HOME="" $DEBUG_FLAG1 $DEBUG_FLAG2
make DEST=`pwd`/binaries ROOT_PATH=`pwd` HOME="" ERRMSG="/usr/share/mysql/english" $DEBUG_FLAG1 $DEBUG_FLAG2  install

%post
ln -s /lib64/libaio.so.1 /lib64/libaio.so
/sbin/ldconfig

%install
mkdir -p $RPM_BUILD_ROOT/etc/ld.so.conf.d/
mkdir -p $RPM_BUILD_ROOT%{install_path}
cp -r binaries/* $RPM_BUILD_ROOT%{install_path}
cp maxscale.conf $RPM_BUILD_ROOT/etc/ld.so.conf.d/

%clean

%files
%defattr(-,root,root)
%{install_path}
/etc/ld.so.conf.d/maxscale.conf

%changelog
