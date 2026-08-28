# CI on a self-hosted macOS runner

The suite's macOS CI jobs run on **your own Mac**, not GitHub-hosted runners.

## Why

GitHub bills macOS minutes at **10×** on private repos. The free tier's 2,000 min/month is
therefore only ~200 real macOS minutes — and this workflow runs ~14 plugin builds plus a
root configure on macOS. A single full run can consume most of the monthly quota, after
which GitHub kills every job *before any step executes* (the tell: jobs "fail" in seconds
with zero steps completed and no logs).

A self-hosted runner makes those jobs **free and unlimited**, and faster — it is the same
machine that already builds all 45 plugins.

The cheap gates (structure, brand sync, pure-C++ core contracts) deliberately stay on
`ubuntu-latest`: they need no macOS, and Linux bills at 1×.

## One-time setup

In the repo on GitHub: **Settings → Actions → Runners → New self-hosted runner**, choose
**macOS** + your architecture (Apple Silicon = `arm64`). GitHub shows a token-bearing
command block — run it on this Mac. It looks like:

```bash
mkdir -p ~/actions-runner && cd ~/actions-runner
curl -o actions-runner-osx-arm64.tar.gz -L <url-github-shows-you>
tar xzf ./actions-runner-osx-arm64.tar.gz
./config.sh --url https://github.com/armonon/sattari-plugins-suite --token <token-github-shows-you>
```

Accept the defaults at the prompts. The runner automatically gets the labels
`self-hosted`, `macOS`, and `ARM64` — which is what `runs-on: [self-hosted, macOS]` in
`.github/workflows/ci.yml` targets.

Then install it as a service so it survives reboots and runs without a terminal open:

```bash
cd ~/actions-runner && ./svc.sh install && ./svc.sh start
```

Check it is live: `./svc.sh status`, or the repo's **Settings → Actions → Runners** page
should show it **Idle** (green).

## Prerequisites on the Mac

Already satisfied on this machine, listed for a rebuild:

- Xcode Command Line Tools (`xcode-select --install`)
- Homebrew + `ninja` and `cmake` (the workflow installs `ninja` only if missing)
- `git` with access to the repo's submodules (JUCE lives in `third_party/JUCE`)

## Behaviour worth knowing

- **Jobs run one at a time.** A single runner is serial, so the ~14-job matrix executes
  sequentially. It is free, but a full run takes a while. To parallelise, register more
  runners (repeat the setup in `~/actions-runner-2`, etc.) — the matrix will spread across
  them automatically.
- **Each job starts from a clean tree.** `actions/checkout` runs `git clean -ffdx`, which
  removes ignored directories including `build/`. That is intentional: a from-scratch build
  is what catches "I forgot to commit that file" and submodule breakage — the main thing
  cloud CI was buying us. It also means no incremental speedup between runs.
- **Builds use every core** (`-j$(sysctl -n hw.ncpu)`), which is right because jobs are
  serial rather than competing for the machine.
- The runner's workspace lives under `~/actions-runner/_work/` — separate from your working
  copy at `~/Projects/sattari-plugins-main`, so CI never disturbs local work.

## Security

Self-hosted runners execute whatever a workflow says. That is fine here because the repo is
**private** — only you can push code that runs.

**Do not make this repo public while a self-hosted runner is attached.** On a public repo,
anyone's pull request would execute arbitrary code on your Mac. If the repo is ever opened
up, remove the runner first and move these jobs back to `macos-latest`.

## Reverting

To go back to GitHub-hosted macOS runners, change both occurrences in
`.github/workflows/ci.yml`:

```yaml
runs-on: [self-hosted, macOS]   # ->  runs-on: macos-latest
```

…and be ready for the 10× minute burn again.
