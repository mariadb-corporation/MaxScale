%define _topdir         %(echo $PWD)/
%define name            rabbitmq-message-consumer
%define release         beta
%define version         1.0
%define install_path    /usr/local/mariadb/rabbitmq-consumer/

BuildRoot:              %{buildroot}
Summary:                rabbitmq-message-consumer
License:                GPL
Name:                   %{name}
Version:                %{version}
Release:                %{release}
Source:                 %{name}-%{version}-%{release}.tar.gz
Prefix:                 /
Group:                  Development/Tools
Requires:        	maxscale

%if 0%{?suse_version}
BuildRequires: gcc gcc-c++ ncurses-devel bison glibc-devel cmake libgcc_s1 perl make libtool libopenssl-devel libaio libaio-devel mariadb libedit-devel librabbitmq-devel MariaDB-shared
%else
BuildRequires: gcc gcc-c++ ncurses-devel bison glibc-devel cmake libgcc perl make libtool openssl-devel libaio libaio-devel librabbitmq-devel MariaDB-shared
%if 0%{?rhel}  == 6
BuildRequires: libedit-devel
%endif
%if 0%{?rhel}  == 7
BuildRequires: mariadb-devel mariadb-embedded-devel libedit-devel
%else
BuildRequires: MariaDB-devel MariaDB-server
%endif
%endif

%description
rabbitmq-message-consumer

%prep

%setup -q

%build
make clean
make

%install
mkdir -p $RPM_BUILD_ROOT%{install_path}
cp consumer $RPM_BUILD_ROOT%{install_path}
cp consumer.cnf $RPM_BUILD_ROOT%{install_path}

%clean

%files
%defattr(-,root,root)
%{install_path}/consumer
%{install_path}/consumer.cnf

%changelog
