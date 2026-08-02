Name:           ajazzy
Version:        %{_version}
Release:        1%{?dist}
Summary:        Configure AJAZZ gaming mice on Linux
License:        MIT
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  gcc, gtk4-devel, libadwaita-devel, gettext
Requires:       gtk4, libadwaita

%description
A reverse-engineered CLI (ajazzyctl) and GTK4 GUI (ajazzy-gui) for
setting DPI, report rate, RGB lighting, button remapping and macros
on AJAZZ mice, without the official Windows-only driver.

%prep
%setup -q

%build
make

%install
rm -rf %{buildroot}
install -Dm755 ajazzyctl %{buildroot}%{_bindir}/ajazzyctl
install -Dm755 ajazzy-gui %{buildroot}%{_bindir}/ajazzy-gui
install -Dm644 gui/io.github.ajazzy.Gui.desktop %{buildroot}%{_datadir}/applications/io.github.ajazzy.Gui.desktop
install -Dm644 gui/icons/io.github.ajazzy.Gui.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/io.github.ajazzy.Gui.svg
install -Dm644 locale/pt_BR/LC_MESSAGES/ajazzy.mo %{buildroot}%{_datadir}/locale/pt_BR/LC_MESSAGES/ajazzy.mo
install -Dm644 udev/71-ajazzy.rules %{buildroot}%{_prefix}/lib/udev/rules.d/71-ajazzy.rules

%files
%{_bindir}/ajazzyctl
%{_bindir}/ajazzy-gui
%{_datadir}/applications/io.github.ajazzy.Gui.desktop
%{_datadir}/icons/hicolor/scalable/apps/io.github.ajazzy.Gui.svg
%{_datadir}/locale/pt_BR/LC_MESSAGES/ajazzy.mo
%{_prefix}/lib/udev/rules.d/71-ajazzy.rules

%post
udevadm control --reload-rules || :

%changelog
* Releases are cut automatically by .github/workflows/release.yml -- see
  the GitHub release notes for what changed in each version, this file
  isn't kept up to date by hand.
