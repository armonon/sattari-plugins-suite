# Native audio website integration — 11 October 2026

Published Netlify release `6acbb6320f93df2bd0c69841` updates only `/index.html`
and `/modular.html` on https://sattari-audio-suite.netlify.app.
StemDeck and Bounce now open their published Creating Co. domains; both pages
link back to the company app directory. Modular availability reflects the
universal Mac build and strict VST3 evidence recorded in the previous release.
Public native installers remain closed pending AU registration, supported DAWs,
listening approval, licensing, commercial signing, notarization and Windows checks.

The current published design, including the removed interactive chain builder,
was preserved through an additive overlay. Do not deploy the entire historical
`apps/sattari-web` directory to reconstruct this release. The portable
`scripts/prepare-audio-web-update.py` prepares edits from freshly captured,
immutable production pages. Its deploy plan requires publication comparison
before promotion. Netlify pretty-URL processing rewrites served HTML; the preparer
checks repeated immutable responses and records stored and served SHA1 values.
All 69 other file hashes were verified unchanged; there are no server functions.

Desktop 1280px and mobile 390/320px checks passed on both draft and production:
canonical links, company return navigation, product search, release status,
no horizontal page overflow and no JavaScript page errors. These are browser
checks rather than physical-device certification.

The older Tracktion JUCE8 patch is already covered by current account develop
`494e91d2ff546353b69723a5e992dd71d1a0204b`: all nine modified audio-format and
plugin-scanning paths use the updated APIs. The old patch needs no additional
port. This source review does not qualify a native engine release.

Digital Human is excluded at the user's request.

The default branch still contains the earlier 0.1.1 marketing page. This source
follow-up adds navigation and the fresh-capture preparer without attributing
0.2.0 qualification to 0.1.1. Current 0.2.0 consolidated native source remains
in PR 5. The hosted status update applies to the current 0.2.0 page only.
