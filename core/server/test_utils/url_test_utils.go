package test_utils

import (
	"context"
	"net/http"
	"time"

	"ThroneCore/internal/boxbox"

	"github.com/sagernet/sing-box/adapter"
)

var URLReporter resultBuffer[URLTestResult]

const URLTestTimeout = 3 * time.Second

type URLTestResult struct {
	Duration time.Duration
	Tag      string
	Error    error
}

func BatchURLTest(ctx context.Context, i *boxbox.Box, outboundTags []string, url string, method string, maxConcurrency int, twice bool, timeout time.Duration) []*URLTestResult {
	if timeout <= 0 {
		timeout = URLTestTimeout
	}
	// Пустой метод — GET: так проба вела себя до того, как метод стал выбором.
	if method == "" {
		method = http.MethodGet
	}

	results := runBatch(ctx, i, outboundTags, maxConcurrency, batchProbe[URLTestResult]{
		run: func(ctx context.Context, tag string, outbound adapter.Outbound) *URLTestResult {
			client := outboundHTTPClient(ctx, outbound, timeout)
			duration, err := urlTest(ctx, client, url, method)
			if err == nil && twice {
				duration, err = urlTest(ctx, client, url, method)
			}
			return &URLTestResult{Duration: duration, Tag: tag, Error: err}
		},
		fail: func(tag string, err error) *URLTestResult {
			return &URLTestResult{Tag: tag, Error: err}
		},
		publish: URLReporter.AddResult,
	})
	URLReporter.Reclaim(results)
	return results
}

func urlTest(ctx context.Context, client *http.Client, url string, method string) (time.Duration, error) {
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
