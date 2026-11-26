%global debug_package %{nil}

Name:           gorgon-editor
Version:        %{?_version}%{!?_version:4.2.0}
Release:        1%{?dist}
Summary:        Standalone 2D Scene Editor built with Gorgon

License:        MIT
URL:            https://github.com/Efendiminati/Gorgon

# Runtime dependencies beyond auto-detected ELF sonames
Requires:       hicolor-icon-theme

%description
Gorgon 2D Editor is a standalone 2D scene editor built with Gorgon itself
(Gorgon::UI widgets, Layerbox viewport, JSON serialization, and GScript VM).
Includes GorgonEditor, GorgonPlayer for exported bundles, and the DemoScene.

%install
rm -rf %{buildroot}

# Binaries live together in libexec so GorgonEditor finds GorgonPlayer beside /proc/self/exe
install -d %{buildroot}%{_libexecdir}/gorgon-editor
install -m 0755 %{_stagedir}/GorgonEditor %{buildroot}%{_libexecdir}/gorgon-editor/GorgonEditor
install -m 0755 %{_stagedir}/GorgonPlayer %{buildroot}%{_libexecdir}/gorgon-editor/GorgonPlayer

# Symlinks on PATH
install -d %{buildroot}%{_bindir}
ln -s %{_libexecdir}/gorgon-editor/GorgonEditor %{buildroot}%{_bindir}/gorgon-editor
ln -s %{_libexecdir}/gorgon-editor/GorgonPlayer %{buildroot}%{_bindir}/gorgon-player

# Shared data: DemoScene and documentation
install -d %{buildroot}%{_datadir}/gorgon-editor/DemoScene
cp -a %{_stagedir}/DemoScene/. %{buildroot}%{_datadir}/gorgon-editor/DemoScene/
install -m 0644 %{_stagedir}/README.md %{buildroot}%{_datadir}/gorgon-editor/README.md

# Desktop entry, icon, and MIME association for .gscene.json
install -d %{buildroot}%{_datadir}/applications
install -m 0644 %{_stagedir}/gorgon-editor.desktop %{buildroot}%{_datadir}/applications/gorgon-editor.desktop

install -d %{buildroot}%{_datadir}/icons/hicolor/256x256/apps
install -m 0644 %{_stagedir}/gorgon-editor.png %{buildroot}%{_datadir}/icons/hicolor/256x256/apps/gorgon-editor.png

install -d %{buildroot}%{_datadir}/mime/packages
install -m 0644 %{_stagedir}/gorgon-scene.xml %{buildroot}%{_datadir}/mime/packages/gorgon-scene.xml

%files
%{_bindir}/gorgon-editor
%{_bindir}/gorgon-player
%dir %{_libexecdir}/gorgon-editor
%{_libexecdir}/gorgon-editor/GorgonEditor
%{_libexecdir}/gorgon-editor/GorgonPlayer
%dir %{_datadir}/gorgon-editor
%{_datadir}/gorgon-editor/DemoScene
%{_datadir}/gorgon-editor/README.md
%{_datadir}/applications/gorgon-editor.desktop
%{_datadir}/icons/hicolor/256x256/apps/gorgon-editor.png
%{_datadir}/mime/packages/gorgon-scene.xml

%changelog
* Thu Nov 26 2026 Gorgon Library <noreply@github.com> - 4.2.0-1
- Initial Fedora RPM package for Gorgon 2D Editor
