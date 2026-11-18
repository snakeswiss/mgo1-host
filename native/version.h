// The build's name, shared by MGO1.exe, MGO1_core.dll, the MGS3 menu entry (MGO1Loader.dll) and
// the dedicated host (mgo1host.exe): the first line of MGO1.log, MGO1Loader.log and the host's and
// the playable dolls' logs, the launcher's label, the update screen. tools/release_check.py checks
// every binary of a package carries it. (build 1.00) The release after v150-host beta 25: shown as
// it is, in every language ("build 1.00", nothing before it).
#pragma once
#define MGO1_BUILD "build 1.00"
// the client's update version code (docs/UPDATER_SETUP.md 2.4, docs/UPDATER_PROTOCOL.md: monotonic,
// compared with the manifest's). The updater uses it unless [update] version_code overrides it (the
// simulation sets it to the fixture baseline). Keep it in step with the build at each release:
// build 1.xy is 1000 + xy (build 1.00 = 1000, build 1.01 = 1001 ...), above v150's 150, so the
// codes stay monotonic.
#define MGO1_VERSION_CODE 1000
