"""Prepare a two-page overlay from a freshly captured immutable Netlify release.

Usage: python3 scripts/prepare-audio-web-update.py BASELINE_JSON OUTPUT_DIRECTORY
The baseline includes site_id, deploy and the complete file manifest. The
publication helper must compare expected_previous before publishing the draft.
Netlify pretty-URL processing changes served HTML relative to stored SHA1;
this script checks two identical immutable responses and records both hashes.
"""
import hashlib
import json
from pathlib import Path
import sys
import urllib.request

REPLACEMENTS = {
    'Locally tested on Apple silicon': 'Universal builds tested on Apple silicon and Intel through Rosetta',
    'Current automated native contracts and VST3 validation are part of development verification. Universal builds are supported by the build pipeline.': 'Modular FX and Instrument build as universal AU, VST3 and standalone bundles. Processor, interface and hosting checks pass on both architectures; both VST3 editions pass strict pluginval validation.',
    'Fresh Intel and AU qualification, commercial signing, Apple notarization and broader DAW testing remain pending for this version.': 'AU registration, commercial signing, Apple notarization, supported DAW sessions and listening approval remain pending for this version.',
    'https://stemdeck-studio.netlify.app/': 'https://stemdeck.thecreatingco.com/studio',
    'https://bounce-music-player.netlify.app/browser/': 'https://bounce.thecreatingco.com/browser/',
    '<span>Independent sound. Shared possibilities.</span>': '<a href="https://thecreatingco.com/downloads/#app-directory">All Creating Co. apps ↗</a>',
}

def main():
    baseline = json.loads(Path(sys.argv[1]).read_text())
    root = Path(sys.argv[2]).resolve()
    root.mkdir(parents=True, exist_ok=True)
    changes, replace = [], {}
    for name in ['index.html', 'modular.html']:
        path = '/' + name
        url = baseline['deploy']['deploy_ssl_url'] + path
        old = urllib.request.urlopen(url, timeout=30).read()
        if urllib.request.urlopen(url, timeout=30).read() != old:
            raise SystemExit('Immutable served HTML changed: ' + path)
        text = old.decode()
        for before, after in REPLACEMENTS.items():
            text = text.replace(before, after)
        if text == old.decode():
            raise SystemExit('No expected update found: ' + path)
        dest = root / name
        dest.write_text(text)
        replace[path] = str(dest)
        original = next(f for f in baseline['files'] if f['path'] == path)
        changes.append({'path': path, 'old_manifest_sha1': original['sha'],
                        'old_served_sha1': hashlib.sha1(old).hexdigest(),
                        'new_sha1': hashlib.sha1(dest.read_bytes()).hexdigest()})
    plan = {'site_id': baseline['site_id'], 'expected_previous': baseline['deploy']['id'],
            'title': 'Connect native audio suite to current company apps and qualified Mac build status',
            'replace': replace, 'functions': {}, 'function_schedules': [],
            'preserve_function_config': True, 'publish_blockers': []}
    (root / 'plan.json').write_text(json.dumps(plan, indent=2))
    (root / 'changes.json').write_text(json.dumps(changes, indent=2))
    print('Prepared two additive HTML replacements; other files retained.')

if __name__ == '__main__':
    main()
