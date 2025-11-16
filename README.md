# wayback_recon – Fast CDX API Wrapper in C

Lightweight, single-binary **C** program that fetches and deduplicates URLs + query parameters from the Wayback Machine (archive.org CDX API).  
Built for speed and bug bounty reconnaissance.

[![Demo](https://asciinema.org/a/fVDtg68ejXoWsa5yqSxvDKMgf.svg)](https://asciinema.org/a/fVDtg68ejXoWsa5yqSxvDKMgf)

## Features
- Blazing fast (pure C, no scripting overhead)
- Direct queries to official CDX API
- Automatic deduplication of endpoints & parameters
- Filter by year or fetch full history (`*`)
- Zero runtime dependencies after compilation

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
./wayback_recon tesla.com

## Usage
```bash
./wayback_recon <domain> [year]

Examples
./wayback_recon tesla.com          # Last ~12 months
./wayback_recon tesla.com 2024     # Only 2024 snapshots
./wayback_recon tesla.com "*"      # Full history (be patient!)
```
## Output

- <domain>_urls.txt   → All unique URLs
- <domain>_params.txt → Extracted parameters only

## Author
@Israel Thomas – Security Engineer | India
X/Twitter • GitHub

Made in India with ☕
⭐ Star if you found it useful • Contributions welcome!
