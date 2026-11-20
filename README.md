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

iCloud Private Relay can be switched off for the network you are on while the tunnel is up, and whatever the setting was is put back when you disconnect. The relay does not step aside for a plain `utun` the way it does for a VPN with a system extension, so Safari would otherwise keep reaching sites through Apple instead of the country the profile picked. It is off by default: Apple does not offer the relay in every country, and where it is not offered there is nothing to switch off, while the system still tells the person that their own network is incompatible with Private Relay. The switch is under DNS.

### Note on the direct DNS

Two resolvers are configured, one for names that travel through the tunnel and one for everything that goes direct. The direct one also turns the server's own hostname into an address, and it does that for every new connection rather than once at startup, so when it stops answering nothing connects at all while the client still reports itself as connected.

Two things it therefore cannot be. Not port 53: strict route, which is on by default on Windows, installs a firewall filter that blocks port 53 on every interface except the tunnel, and the filter matches on the port, not on the process, so the core's own queries are blocked with everyone else's. And not a server named by hostname: that name would have to be resolved by the system resolver, which the tunnel has already captured, and it leads straight back to the server name we are trying to resolve. The default is therefore DoT to an address, and the server list in `res/dns_servers.json` keeps every entry that way. A file of the same name next to the settings overrides it.

Before each connect the configured resolver and the alternatives are queried in parallel, with the hostname of the server about to be dialled rather than some neutral name, and whichever answers first is the one the session uses. A resolver that answers one probe and then falls apart is the normal case where this audience lives, so the round is run every time rather than only when the configured one has already failed. Turn it off with "Switch automatically when it stops answering" under DNS.

### Note on subscription updates while connected

An update never drops the connection. When the subscription no longer carries the server the tunnel is running on, that profile leaves the list straight away, so it does not stand next to the one that replaced it, but its session keeps going until you disconnect. The moment you do, it is deleted and the selection moves to its successor: the profile with the same name, or, when the name only differs by a tag such as a speed or a flag, the one that matches without it, and otherwise the first in the group.

### Note on updates that install themselves

With automatic updates on, the client downloads the new version, restarts and brings the connection back to the server it was on. It also comes back the way it was left: a window closed to the tray stays in the tray, a minimized window comes back minimized, and an open window opens as before, so an update that runs while you are working does not put a window in front of it. Once the new version is running, the client says so one time — a line in the window if you are in it, a system notification if you are not.

### Note on a tunnel Windows will not start

On Windows the adapter is created by Wintun, and when Windows will not hand one over there is nothing in the client to fix. The client therefore asks the device manager before it says anything, both before a start and after one that failed, and names what it found: Windows unable to finish setting up new network devices at all, which it reports as device problem code 56 and which happens to every new network device on that machine, a driver installation put off until the next restart, an adapter disabled by hand, an adapter the previous attempt left behind and Windows is still unloading, or a driver registered at a path with no file at it, which is usually an antivirus having deleted it. A start that cannot succeed is refused right away rather than waiting out the fifteen seconds Wintun spends asking, because each failed attempt leaves behind a device operation that Windows takes minutes to finish and the next attempt queues another one behind it. An adapter merely left over from an earlier session is not refused, since Wintun takes those over by itself.

The same answers go into the support report, under `02a_net_devices.txt`: every network-class device with its state and problem code, the tunnel driver service with the path it is registered at, the size, version and signature of the file, the network filter drivers that are running, and the network-class drivers in the driver store, where a failed installation leaves duplicates behind. A machine that cannot configure network devices is repaired by installing Windows over itself with the files and applications kept, and nothing the client does changes it.

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

## Subscription response headers

A panel can configure the client through the headers of its subscription
response. Everything below is what this fork actually reads, and the whole set
is kept with the group it came from, so the next update does not have to repeat
it. A header name's case does not matter, and any value may arrive as
`base64:<payload>` when it carries non-ASCII text or commas. A header sent twice
is read as its last value, so a proxy in front of a panel can override what the
panel itself sends.

### What the panel can say

| Header | Effect |
| --- | --- |
| `profile-title` | The name of the group in the client. |
| `announce` | A line shown with the subscription. Panels use it to say why a list came back empty. |
| `support-url` | Where the client sends a user asking for help. |
| `profile-web-page-url` | The panel's own page for this subscription. |
| `profile-update-interval` | Hours between automatic updates. `x-profile-update-interval` is accepted too. It is followed only when the client, or the group, is set to respect the server's interval. |
| `subscription-userinfo` | Traffic and expiry, as `upload=`, `download=`, `total=`, `expire=`. Feeds the usage bar and the expiry date. |
| `subscription-refill-date` | Unix seconds. Shown as the date the quota refills. |
| `tun-enable` | Puts the client into VPN mode, or takes it out, when a server of this subscription connects. |
| `hide-settings` | Hides the settings while a server of this subscription runs, and with them the server configuration. |
| `hide-url` | Hides the subscription address, and with it the share links of its servers: a `vless://` link carries the same credentials. |
| `subscription-pin` | The group cannot be deleted while a server of it runs. Other subscriptions stay deletable. |
| `subscription-auto-update-enable` | Turns automatic subscription updates on or off. |
| `subscription-always-hwid-enable` | Sends the device id with every subscription request. |
| `dns-from-json-enable` | `false` stops the client from taking the DNS section out of a provider's whole-config profile. |
| `subscription-ping-onopen-enabled` | Tests latency as soon as the server list is opened. |
| `per-app-proxy-list` | Applications to route through the tunnel. Separated by commas, semicolons or newlines, a `processName:` prefix is tolerated, and the whole value may be base64 or a JSON array. |
| `per-app-bypass-list` | Applications to leave outside the tunnel. Providers use it for the traffic they are not allowed to carry. |
| `routing` | A routing profile link, imported when the group has the import of a subscription route turned on. `off` means the header carries nothing, and the link may then sit in the body instead. |
| `x-fsnt-sub-domain` | Moves the subscription to another host. See below. |
| `x-fsnt-sub-mirrors` | Hosts to fall back to when the current one stops answering. See below. |

`providerid` and `subscriptions-collapse` are read and kept, but nothing in the
client acts on them yet. Any other header that is not an ordinary server header
is kept with the group as well, so a panel can send something this version does
not know yet and a later one will find it.

A switch is three-valued: a header that is absent is not `false`. `true`, `1`,
`yes` and `on` turn it on, `false`, `0`, `no` and `off` turn it off, and nothing
at all leaves the client's own setting alone. The restrictions — hiding and
pinning — hold only while a server of that subscription is running, so stopping
it always gives the user the full interface back.

An update that answers without any of these headers does not wipe what was
stored before: a panel may drop them for one response, and the client keeps the
last set it was given.

Panels that cannot set headers can put the same values in the first lines of the
subscription body, as `#subscription-userinfo: ...` or `// profile-title: ...`.
Only `subscription-userinfo`, `profile-update-interval`, `profile-title`,
`profile-web-page-url`, `support-url` and `announce` (or `notice`) are read that
way, and a header always wins over the comment.

### Moving a subscription to another domain

A blocked domain is the usual reason a subscription stops working, and these two
headers let the panel deal with it without the user retyping anything.

`x-fsnt-sub-domain` names the host the subscription lives on from now on. The
client replaces the host of the stored address, and the port when the header
names one, keeping the path and the query untouched because the subscriber's
token is in them. Before the new address is stored it is fetched once and has to
answer with servers, so a typo in a panel's configuration cannot cut every
client off at once.

`x-fsnt-sub-mirrors` lists hosts to try when the current address does not answer
at all. The list is stored while the subscription is still reachable, which is
the point: once a domain is blocked there is nothing left to read a header from.
The mirror that answers becomes the address the group updates from, so the
list is worth keeping complete on every host it is served from, the current
one included: a client that has moved to a mirror learns its next list from
that mirror.

An address that does not answer is asked once more by the other path before the
next address is tried: through the tunnel when the first attempt went out
directly, and past the tunnel when it did not, because a tunnel that is up and
carrying nothing takes the update down with it. In Tun Mode both paths end in
the same tunnel, so nothing is repeated there, and with no profile running there
is nowhere to repeat it through.

Both are honoured only on an `https` subscription. On plain HTTP anyone on the
path could write them, and a move is permanent. The value has to be a bare host
or `host:port` — anything carrying a scheme, a path, a query or credentials is
refused, and so is a single-label name such as `localhost`.

### Example

The headers are a property of the response, so they are usually set in front of
the panel rather than in it. `always` matters: without it nginx drops them from
error responses, which is exactly when an announcement is worth reading. A
header the panel already sends does not have to be hidden first, because the one
added in front of it comes last and wins.

```nginx
location /sub/ {
    proxy_pass http://127.0.0.1:3010;

    add_header Profile-Title "base64:RlNOVCBWUE4=" always;
    add_header Profile-Update-Interval "6" always;
    add_header Support-Url "https://t.me/example_support" always;
    add_header Announce "base64:0J/Qu9Cw0L3QvtCy0YvQtSDRgNCw0LHQvtGC0Ysg0LIg0YHRg9Cx0LHQvtGC0YMg0YEgMDI6MDAg0LTQviAwNDowMA==" always;
    add_header Subscription-Pin "true" always;
    add_header Hide-Url "true" always;
    add_header Per-App-Bypass-List "qbittorrent.exe,transmission.exe" always;

    # Where to go when this domain stops answering.
    add_header X-Fsnt-Sub-Mirrors "sub2.example.com, sub3.example.com:8443" always;
}
```

Announcing a move, on the old domain and while it still answers:

```nginx
add_header X-Fsnt-Sub-Domain "sub2.example.com" always;
```

What a client got can be read back from the command line:

```bash
curl -s -D - -o /dev/null "https://sub.example.com/sub/<token>" | grep -iE 'profile-|subscription-|announce|x-fsnt-'
```

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
