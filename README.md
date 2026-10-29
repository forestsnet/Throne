# Throne

Qt based Desktop cross-platform GUI proxy utility, empowered by [Sing-box](https://github.com/SagerNet/sing-box)

Supports Windows 11/10/8/7 / Linux / MacOS out of the box.

Now supporting headless mode, running as a service or in a container, controlled with [thronectl](https://github.com/throneproj/Throne-api): see [Headless Throne](https://throneproj.github.io/headless/).

<img width="1002" height="789" alt="image" src="https://github.com/user-attachments/assets/af4a8e32-7e55-430c-9402-ec2d665cf71a" />

### Supporting Throne
If Throne is useful to you, you can support its development with [crypto](.github/donate/Donate.md) (USDT, USDC, TRX or Monero) or through [GitHub Sponsors](https://github.com/sponsors/Mahdi-zarei).

### Note on MacOS releases
Apple platforms have a very strict security policy and since Throne does not have a signed certificate, you will have to remove the quarantine using `xattr -d com.apple.quarantine /path/to/throne.app`. Move `Throne.app` to `/Applications` before the first launch. The [Homebrew cask](#macos-homebrew) does both for you.

Opening a TUN device needs root, so the app asks macOS for your password and makes the core setuid-root. That permission belongs to one file, and every update installs a new core, so it would be asked for again every time — including after an update that installs itself while you are away, which leaves the VPN down until you come back. The prompt therefore offers to grant it for good: a `moe.Throne.coreprivilege` LaunchDaemon watches the core and puts the permission back whenever the file changes, and nothing is asked again.

The trade-off is worth stating plainly: while that service is installed, anyone who can write inside `FSNT Client.app` gains root without a password. Remove it under Settings -> Connection, or by hand:

```sh
sudo launchctl bootout system/moe.Throne.coreprivilege
sudo rm -f /Library/LaunchDaemons/moe.Throne.coreprivilege.plist
sudo chmod u-s "/Applications/FSNT Client.app/Contents/MacOS/ThroneCore"
```

While the tunnel is up, iCloud Private Relay is switched off for the network you are on. The relay does not step aside for a plain `utun` the way it does for a VPN with a system extension, so Safari would otherwise keep reaching sites through Apple instead of the country the profile picked. Whatever the setting was is put back when you disconnect, and there is a switch for the whole thing under DNS.

### Note on the direct DNS

Two resolvers are configured, one for names that travel through the tunnel and one for everything that goes direct. The direct one also turns the server's own hostname into an address, and it does that for every new connection rather than once at startup, so when it stops answering nothing connects at all while the client still reports itself as connected.

Two things it therefore cannot be. Not port 53: strict route, which is on by default on Windows, installs a firewall filter that blocks port 53 on every interface except the tunnel, and the filter matches on the port, not on the process, so the core's own queries are blocked with everyone else's. And not a server named by hostname: that name would have to be resolved by the system resolver, which the tunnel has already captured, and it leads straight back to the server name we are trying to resolve. The default is therefore DoT to an address, and the server list in `res/dns_servers.json` keeps every entry that way. A file of the same name next to the settings overrides it.

Before each connect the configured resolver and the alternatives are queried in parallel, with the hostname of the server about to be dialled rather than some neutral name, and whichever answers first is the one the session uses. A resolver that answers one probe and then falls apart is the normal case where this audience lives, so the round is run every time rather than only when the configured one has already failed. Turn it off with "Switch automatically when it stops answering" under DNS.

### GitHub Releases (Portable ZIP)

[![GitHub All Releases](https://img.shields.io/github/downloads/throneproj/Throne/total?label=downloads-total&logo=github&style=flat-square)](https://github.com/throneproj/Throne/releases)

### Linux CLI installer
```bash
curl -fsSL https://raw.githubusercontent.com/throneproj/Throne/dev/script/install_linux.py | sudo python3
```

### Linux package repositories
Every stable release is published to signed APT and RPM repositories (the system-Qt packages, x64 and ARM64) and to the AUR, so your package manager keeps Throne up to date. See the [installation guide](https://throneproj.github.io/get_started/installation/#apt-repository) for details.

Debian / Ubuntu:
```bash
sudo install -d -m 0755 /etc/apt/keyrings
sudo curl -fsSLo /etc/apt/keyrings/throne.asc https://throneproj.github.io/Throne/throne.asc
sudo curl -fsSLo /etc/apt/sources.list.d/throne.list https://throneproj.github.io/Throne/apt/throne.list
sudo apt update && sudo apt install throne
```

Fedora / RHEL 9+:
```bash
sudo curl -fsSLo /etc/yum.repos.d/throne.repo https://throneproj.github.io/Throne/rpm/throne.repo
sudo dnf install throne --refresh
```

openSUSE / SLES:
```bash
sudo zypper addrepo -fc https://throneproj.github.io/Throne/rpm/throne.repo
sudo zypper install throne
```

Arch Linux (AUR): `yay -S throne` or `paru -S throne`

### macOS (Homebrew)
Every stable release is also published to Throne's own Homebrew tap (macOS 14.4 or newer, Apple Silicon and Intel). The cask installs `Throne.app` to `/Applications` and removes the quarantine flag; update with `brew upgrade --cask throne`. See the [installation guide](https://throneproj.github.io/get_started/installation/#homebrew) for details.
```bash
brew install --cask throneproj/tap/throne
```

## Supported protocols

- SOCKS
- HTTP(S)
- Shadowsocks
- Trojan
- VMess
- VLESS
- TUIC
- Hysteria
- Hysteria2
- AnyTLS
- Mieru
- Snell
- NaïveProxy
- Juicity
- TrustTunnel
- ShadowTLS
- Wireguard
- AmneziaWG
- MASQUE
- SSH
- Xray VLESS
- OpenVPN/OpenConnect
- Custom Outbound (Both Sing-box and Xray)
- Custom Config (Both Sing-box and Xray)
- Chaining outbounds
- Extra Core

## Subscription Formats

Various formats are supported, including share links, various JSON representation of Sing-box configs, and v2rayN link format as well as limited support for Shadowsocks and Clash formats.

## Deep links

The client registers the `throne://` URL scheme, so a link on a web page, in a
chat message or in a desktop shortcut can drive it. Clicking one hands the URL
to the already running instance; if the client is not running it starts first
and then acts on the link.

| Link | What it does |
| --- | --- |
| `throne://subscribe?url=<url>` | Add (or refresh) a subscription. The URL must be percent-encoded. If a subscription for the same host already exists it is updated in place rather than duplicated. |
| `throne://addsub/<base64url>` | The same, with `<url>#<name>` packed into base64url. |
| `throne://add/<base64url>` | Import share links or a config. |
| `throne://route/<base64url>` | Import one routing profile. |
| `throne://remoteroute/<base64url>` | Import a set of remote routing profiles. |
| `throne://connect` | Connect, using the server the connect button would pick. |
| `throne://disconnect` | Disconnect. |
| `throne://toggle` | Connect if disconnected, disconnect if connected. |

Links that add something ask for confirmation and bring the window forward.
`connect`, `disconnect` and `toggle` deliberately do neither: they are meant for
shortcuts and hotkeys, where a window appearing on top would be in the way. The
tray icon reflects the result.

Upstream Throne documents its own set of deep links
[here](https://throneproj.github.io/advanced/deeplinks/); the table above is
what this fork actually accepts.

## Credits

- [SagerNet/sing-box](https://github.com/SagerNet/sing-box)
- [XTLS/Xray-core](https://github.com/xtls/xray-core)
- [Qv2ray](https://github.com/Qv2ray/Qv2ray)
- [Qt](https://www.qt.io/)
- [simple-protobuf](https://github.com/tonda-kriz/simple-protobuf)
- [fkYAML](https://github.com/fktn-k/fkYAML)
- [quirc](https://github.com/dlbeer/quirc)
- [srombauts/sqlitecpp](https://github.com/srombauts/sqlitecpp)

## FAQ
**How does this project differ from the original Nekoray?** <br/>
Nekoray's developer partially abandoned the project on December of 2023, some minor updates were done recently but the project is now officially archived. This project was meant to continue the way of the original project, with lots of improvements, tons of new features and also, removal of obsolete features and simplifications.

**Why does my Anti-Virus detect Throne and/or its Core as malware?** <br/>
Throne's built-in update functionallity downloads the new release, removes the old files and replaces them with the new ones, which is quite simliar to what malwares do, remove your files and replace them with an encrypted version of your files.

**Is setting the `SUID` bit really needed on Linux?** <br/>
To create and manage a system TUN interface, root access is required, without it, you will have to grant the Core some `Cap_xxx_admin` and still, need to enter your password 3 to 4 times per TUN activation. You can also opt to disable the automatic privilege escalation in `Basic Settings`->`Security`, but note that features that require root access will stop working unless you manually grant the needed permissions.

**Why does my internet stop working after I force quit Throne?** <br/>
If Throne is force-quit while `System proxy` is enabled, the process ends immediately and Throne cannot reset the proxy. <br/>
Solution:
- Always close Throne normally.
- If you force quit by accident, open Throne again, enable `System proxy`, then disable it- this will reset the settings.

**Where are the downloadable route profiles/rulesets coming from?**<br/>
They are located at the [routeprofiles](https://github.com/throneproj/routeprofiles) repository.

**How does "Throne-\<version\>-debian-system-qt-x64.deb" differ from "Throne-\<version\>-debian-x64.deb" and why is the latter 3 times heavier then the former?**<br/>
The first one does not pack the Qt libraries and relies on those installed on the host. The second one packs everything needed with itself, thus being heavier. The reason the first one exists is that on legacy systems provided Qt libraries use unsupported system features. If a graphical interface fails to load for your system, you may try to download the system-qt version and install fitting Qt libraries from your package manager or compile them from source.
