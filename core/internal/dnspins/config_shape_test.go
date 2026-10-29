package dnspins

import (
	"context"
	"testing"

	boxbox "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/include"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common/json"
)

// Конфиг, который собирает generate.cpp для прибитого имени: транспорт hosts
// со всеми адресами и узел, который его этим резолвером и разрешает. Если
// ядро такого не примет, не запустится ни один нативный профиль, а узнать об
// этом на стенде дороже, чем здесь.
func TestPinnedServerConfigParses(t *testing.T) {
	const config = `{
	  "dns": {
	    "servers": [
	      {"type": "tls", "tag": "dns-direct", "server": "77.88.8.8"},
	      {"type": "hosts", "tag": "dns-pinned",
	       "predefined": {"relay-nl.api.fx-srv.org": ["207.89.20.34", "207.89.20.53", "207.89.20.52"]}}
	    ]
	  },
	  "outbounds": [
	    {"type": "vless", "tag": "proxy", "server": "relay-nl.api.fx-srv.org", "server_port": 8443,
	     "uuid": "00000000-0000-0000-0000-000000000000",
	     "domain_resolver": {"server": "dns-pinned"}}
	  ]
	}`
	ctx := boxbox.Context(context.Background(), include.InboundRegistry(), include.OutboundRegistry(),
		include.EndpointRegistry(), include.DNSTransportRegistry(), include.ServiceRegistry(),
		include.CertificateProviderRegistry())
	var options option.Options
	if err := json.UnmarshalContext(ctx, []byte(config), &options); err != nil {
		t.Fatalf("ядро не приняло конфиг: %v", err)
	}
	if len(options.DNS.Servers) != 2 {
		t.Fatalf("серверов DNS: %d", len(options.DNS.Servers))
	}
	hosts := options.DNS.Servers[1]
	if hosts.Type != "hosts" || hosts.Tag != "dns-pinned" {
		t.Fatalf("второй сервер: %+v", hosts)
	}
	predefined := hosts.Options.(*option.HostsDNSServerOptions).Predefined
	if predefined == nil || predefined.Size() != 1 {
		t.Fatalf("таблица пуста")
	}
	for _, entry := range predefined.Entries() {
		if len(entry.Value) != 3 {
			t.Fatalf("адресов сохранилось %d, а было 3", len(entry.Value))
		}
	}
}
