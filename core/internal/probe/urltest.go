package probe

import (
	"context"
	"net/http"
	"time"

	"github.com/sagernet/sing-box/adapter"
)

var URLReporter resultBuffer[URLTestResult]

const URLTestTimeout = 3 * time.Second

type URLTestResult struct {
	Duration time.Duration
	Tag      string
	Error    error
}

func BatchURLTest(ctx context.Context, i Box, outboundTags []string, url string, method string, maxConcurrency int, twice bool, timeout time.Duration) []*URLTestResult {
	results := BatchURLTestTo(ctx, i, outboundTags, url, method, maxConcurrency, twice, timeout, URLReporter.AddResult)
	URLReporter.Reclaim(results)
	return results
}

// BatchURLTestTo hands every finished result to publish; aborted tags are returned but never published.
// method is the HTTP method of the probe; empty means GET, as before the choice existed.
func BatchURLTestTo(ctx context.Context, i Box, outboundTags []string, url string, method string, maxConcurrency int, twice bool, timeout time.Duration, publish func(*URLTestResult)) []*URLTestResult {
	if timeout <= 0 {
		timeout = URLTestTimeout
	}
	if method == "" {
		method = http.MethodGet
	}

	return runBatch(ctx, i, outboundTags, maxConcurrency, batchProbe[URLTestResult]{
		run: func(ctx context.Context, tag string, outbound adapter.Outbound) *URLTestResult {
			if err := awaitTunnels(ctx, i, tag); err != nil {
				return &URLTestResult{Tag: tag, Error: err}
			}
			client, closeClient := outboundHTTPClient(ctx, i, tag, outbound)
			defer closeClient()
			duration, err := urlTest(ctx, client, url, method, firstRequestTimeout(i, tag, twice, timeout))
			if err == nil && twice {
				duration, err = urlTest(ctx, client, url, method, timeout)
			}
			return &URLTestResult{Duration: duration, Tag: tag, Error: err}
		},
		fail: func(tag string, err error) *URLTestResult {
			return &URLTestResult{Tag: tag, Error: err}
		},
		publish: publish,
	})
}

func urlTest(ctx context.Context, client *http.Client, url string, method string, timeout time.Duration) (time.Duration, error) {
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	begin := time.Now()
	req, err := http.NewRequestWithContext(ctx, method, url, nil)
	if err != nil {
		return 0, err
	}
	resp, err := client.Do(req)
	if err != nil {
		return 0, err
	}
	_ = resp.Body.Close()
	return time.Since(begin), nil
}
