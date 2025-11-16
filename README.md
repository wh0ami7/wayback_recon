# wayback_recon – Fast CDX API Wrapper in C

Lightweight, single-binary **C** program that fetches and deduplicates URLs + query parameters from the Wayback Machine (archive.org CDX API).  
Built for speed and bug bounty reconnaissance.

[![Stars](https://img.shields.io/github/stars/wh0ami7/wayback_recon?style=flat&logo=github)](https://github.com/wh0ami7/wayback_recon/stargazers)
[![Forks](https://img.shields.io/github/forks/wh0ami7/wayback_recon?style=flat&logo=github)](https://github.com/wh0ami7/wayback_recon/network/members)
[![Issues](https://img.shields.io/github/issues/wh0ami7/wayback_recon?style=flat&logo=github)](https://github.com/wh0ami7/wayback_recon/issues)
[![License](https://img.shields.io/github/license/wh0ami7/wayback_recon?style=flat&logo=mit)](https://github.com/wh0ami7/wayback_recon/blob/main/LICENSE)
[![Language](https://img.shields.io/github/languages/top/wh0ami7/wayback_recon?style=flat&logo=c)](https://github.com/wh0ami7/wayback_recon)
[![Last Commit](https://img.shields.io/github/last-commit/wh0ami7/wayback_recon?style=flat)](https://github.com/wh0ami7/wayback_recon/commits/main)
[![Release](https://img.shields.io/github/v/release/wh0ami7/wayback_recon?style=flat)](https://github.com/wh0ami7/wayback_recon/releases)

[![Demo](https://asciinema.org/a/fVDtg68ejXoWsa5yqSxvDKMgf.svg)](https://asciinema.org/a/fVDtg68ejXoWsa5yqSxvDKMgf)

## Features
- Blazing fast (pure C, no scripting overhead) – handles thousands of CDX API responses
- Direct queries to official CDX API for reliable, rate-limited access
- Automatic deduplication of endpoints & parameters using efficient hashing
- Filter by year (e.g., 2024) or fetch full history (`*`) – supports wildcard timestamps
- Zero runtime dependencies after static compilation (portable binary)
- Outputs clean, sorted text files: `<domain>_urls.txt` and `<domain>_params.txt`
- Optimized for recon workflows: pipe outputs to tools like `httpx` or `nuclei`
- Lightweight footprint: ~50KB binary, no external libs at runtime

[![Made with C](https://img.shields.io/badge/Made%20with-C-00599C?style=flat&logo=c&logoColor=white)](https://www.cprogramming.com/)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-blue.svg?style=flat)](http://makeapullrequest.com)
[![Support on Ko-fi](https://img.shields.io/badge/Support%20on-Ko--fi-F16061?style=flat&logo=ko-fi&logoColor=white)](https://ko-fi.com/wh0ami7)
[![Twitter Follow](https://img.shields.io/twitter/follow/Wh0ami_7?style=flat&logo=twitter)](https://twitter.com/Wh0ami_7)

## Build from Source

```bash
git clone https://github.com/wh0ami7/wayback_recon.git
cd wayback_recon

# Dependencies (Debian/Ubuntu/Kali)
sudo apt install build-essential libcurl4-openssl-dev libjansson-dev

# Compile (ultra-optimized static binary)
gcc -std=c23 -O3 -march=native -mtune=native -flto -ffast-math \
    -static-libgcc -Wl,-O1,--as-needed,--strip-all \
    -o wayback_recon wayback_recon.c -lcurl -ljansson

# Run
./wayback_recon example.com

## Usage
```bash
./wayback_recon <domain> [year]

Examples
./wayback_recon example.com          # Last ~12 months
./wayback_recon example.com 2024     # Only 2024 snapshots
./wayback_recon example.com "*"      # Full history (be patient!)
```
## Output

- <domain>_urls.txt   → All unique URLs
- <domain>_params.txt → Extracted parameters only

## Author
@Israel Thomas – Security Engineer | India
LinkedIn - https://www.linkedin.com/in/israel7/ • GitHub - https://github.com/wh0ami7

Made in India with ☕
⭐ Star if you found it useful • Contributions welcome!
