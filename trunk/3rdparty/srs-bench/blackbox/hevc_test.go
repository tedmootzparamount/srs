// The MIT License (MIT)
//
// # Copyright (c) 2023 Winlin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
package blackbox

import (
	"context"
	"fmt"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"math/rand"
	"os"
	"path"
	"runtime"
	"sync"
	"testing"
	"time"
)

func TestSlow_RtmpPublish_RtmpPlay_HEVC_Basic(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Only enable for github actions, ignore for darwin.
	if runtime.GOOS == "darwin" {
		logger.Tf(ctx, "Depends on FFmpeg(HEVC over RTMP), only available for GitHub actions")
		return
	}

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5, r6, r7 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5, r6, r7); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var wg sync.WaitGroup
	defer wg.Wait()

	// Start SRS server and wait for it to be ready.
	svr := NewSRSServer()
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream.
	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			// Use the fastest preset of x265, see https://x265.readthedocs.io/en/master/presets.html
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-acodec", "copy", "-vcodec", "libx265",
			"-profile:v", "main", "-preset", "ultrafast", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r1 = ffmpeg.Run(ctx, cancel)
	}()

	// Start FFprobe to detect and verify stream.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(svr.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.ts", streamID))
		v.streamURL = fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), streamID)
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r2 = ffprobe.Run(ctx, cancel)
	}()

	// Fast quit for probe done.
	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()

		str, m := ffprobe.Result()
		if len(m.Streams) != 2 {
			r3 = errors.Errorf("invalid streams=%v, %v, %v", len(m.Streams), m.String(), str)
		}

		// Note that HLS score is low, so we only check duration.
		if dv := m.Duration(); dv < duration {
			r5 = errors.Errorf("short duration=%v < %v, %v, %v", dv, duration, m.String(), str)
		}

		if v := m.Video(); v == nil {
			r5 = errors.Errorf("no video %v, %v", m.String(), str)
		} else if v.CodecName != "hevc" {
			r6 = errors.Errorf("invalid video codec=%v, %v, %v", v.CodecName, m.String(), str)
		}
	}
}

func TestSlow_RtmpPublish_HttpFlvPlay_HEVC_Basic(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Only enable for github actions, ignore for darwin.
	if runtime.GOOS == "darwin" {
		logger.Tf(ctx, "Depends on FFmpeg(HEVC over RTMP), only available for GitHub actions")
		return
	}

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5, r6, r7 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5, r6, r7); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var wg sync.WaitGroup
	defer wg.Wait()

	// Start SRS server and wait for it to be ready.
	svr := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_HTTP_SERVER_ENABLED=on",
			"SRS_VHOST_HTTP_REMUX_ENABLED=on",
			// If guessing, we might got no audio because transcoding might be delay for sending audio packets.
			"SRS_VHOST_HTTP_REMUX_GUESS_HAS_AV=off",
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream.
	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			// Use the fastest preset of x265, see https://x265.readthedocs.io/en/master/presets.html
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-acodec", "copy", "-vcodec", "libx265",
			"-profile:v", "main", "-preset", "ultrafast", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r1 = ffmpeg.Run(ctx, cancel)
	}()

	// Start FFprobe to detect and verify stream.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(svr.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.ts", streamID))
		v.streamURL = fmt.Sprintf("http://localhost:%v/live/%v.flv", svr.HTTPPort(), streamID)
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r2 = ffprobe.Run(ctx, cancel)
	}()

	// Fast quit for probe done.
	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()

		str, m := ffprobe.Result()
		if len(m.Streams) != 2 {
			r3 = errors.Errorf("invalid streams=%v, %v, %v", len(m.Streams), m.String(), str)
		}

		// Note that HLS score is low, so we only check duration.
		if dv := m.Duration(); dv < duration {
			r5 = errors.Errorf("short duration=%v < %v, %v, %v", dv, duration, m.String(), str)
		}

		if v := m.Video(); v == nil {
			r5 = errors.Errorf("no video %v, %v", m.String(), str)
		} else if v.CodecName != "hevc" {
			r6 = errors.Errorf("invalid video codec=%v, %v, %v", v.CodecName, m.String(), str)
		}
	}
}

func TestSlow_RtmpPublish_HttpTsPlay_HEVC_Basic(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Only enable for github actions, ignore for darwin.
	if runtime.GOOS == "darwin" {
		logger.Tf(ctx, "Depends on FFmpeg(HEVC over RTMP), only available for GitHub actions")
		return
	}

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5, r6, r7 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5, r6, r7); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var wg sync.WaitGroup
	defer wg.Wait()

	// Start SRS server and wait for it to be ready.
	svr := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_HTTP_SERVER_ENABLED=on",
			"SRS_VHOST_HTTP_REMUX_ENABLED=on",
			"SRS_VHOST_HTTP_REMUX_MOUNT=[vhost]/[app]/[stream].ts",
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream.
	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			// Use the fastest preset of x265, see https://x265.readthedocs.io/en/master/presets.html
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-acodec", "copy", "-vcodec", "libx265",
			"-profile:v", "main", "-preset", "ultrafast", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r1 = ffmpeg.Run(ctx, cancel)
	}()

	// Start FFprobe to detect and verify stream.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(svr.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.ts", streamID))
		v.streamURL = fmt.Sprintf("http://localhost:%v/live/%v.ts", svr.HTTPPort(), streamID)
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r2 = ffprobe.Run(ctx, cancel)
	}()

	// Fast quit for probe done.
	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()

		str, m := ffprobe.Result()
		if len(m.Streams) != 2 {
			r3 = errors.Errorf("invalid streams=%v, %v, %v", len(m.Streams), m.String(), str)
		}

		// Note that HLS score is low, so we only check duration.
		if dv := m.Duration(); dv < duration {
			r5 = errors.Errorf("short duration=%v < %v, %v, %v", dv, duration, m.String(), str)
		}

		if v := m.Video(); v == nil {
			r5 = errors.Errorf("no video %v, %v", m.String(), str)
		} else if v.CodecName != "hevc" {
			r6 = errors.Errorf("invalid video codec=%v, %v, %v", v.CodecName, m.String(), str)
		}
	}
}

func TestSlow_RtmpPublish_HlsPlay_HEVC_Basic(t *testing.T) {
	// This case is run in parallel.
	t.Parallel()

	// Setup the max timeout for this case.
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	defer cancel()

	// Only enable for github actions, ignore for darwin.
	if runtime.GOOS == "darwin" {
		logger.Tf(ctx, "Depends on FFmpeg(HEVC over RTMP), only available for GitHub actions")
		return
	}

	// Check a set of errors.
	var r0, r1, r2, r3, r4, r5, r6 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3, r4, r5, r6); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var wg sync.WaitGroup
	defer wg.Wait()

	// Start SRS server and wait for it to be ready.
	svr := NewSRSServer(func(v *srsServer) {
		v.envs = []string{
			"SRS_HTTP_SERVER_ENABLED=on",
			"SRS_VHOST_HLS_ENABLED=on",
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		r0 = svr.Run(ctx, cancel)
	}()

	// Start FFmpeg to publish stream.
	streamID := fmt.Sprintf("stream-%v-%v", os.Getpid(), rand.Int())
	streamURL := fmt.Sprintf("rtmp://localhost:%v/live/%v", svr.RTMPPort(), streamID)
	ffmpeg := NewFFmpeg(func(v *ffmpegClient) {
		v.args = []string{
			// Use the fastest preset of x265, see https://x265.readthedocs.io/en/master/presets.html
			"-stream_loop", "-1", "-re", "-i", *srsPublishAvatar, "-acodec", "copy", "-vcodec", "libx265",
			"-profile:v", "main", "-preset", "ultrafast", "-f", "flv", streamURL,
		}
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r1 = ffmpeg.Run(ctx, cancel)
	}()

	// Start FFprobe to detect and verify stream.
	duration := time.Duration(*srsFFprobeDuration) * time.Millisecond * 2
	ffprobe := NewFFprobe(func(v *ffprobeClient) {
		v.dvrFile = path.Join(svr.WorkDir(), "objs", fmt.Sprintf("srs-ffprobe-%v.ts", streamID))
		v.streamURL = fmt.Sprintf("http://localhost:%v/live/%v.m3u8", svr.HTTPPort(), streamID)
		v.duration, v.timeout = duration, time.Duration(*srsFFprobeTimeout)*time.Millisecond * 2
	})
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-svr.ReadyCtx().Done()
		r2 = ffprobe.Run(ctx, cancel)
	}()

	// Fast quit for probe done.
	select {
	case <-ctx.Done():
	case <-ffprobe.ProbeDoneCtx().Done():
		defer cancel()

		str, m := ffprobe.Result()
		if len(m.Streams) != 2 {
			r3 = errors.Errorf("invalid streams=%v, %v, %v", len(m.Streams), m.String(), str)
		}

		// Note that HLS score is low, so we only check duration. Note that only check part of duration, because we
		// might get only some pieces of segments.
		if dv := m.Duration(); dv < duration/3 {
			r4 = errors.Errorf("short duration=%v < %v, %v, %v", dv, duration/3, m.String(), str)
		}

		if v := m.Video(); v == nil {
			r5 = errors.Errorf("no video %v, %v", m.String(), str)
		} else if v.CodecName != "hevc" {
			r6 = errors.Errorf("invalid video codec=%v, %v, %v", v.CodecName, m.String(), str)
		}
	}
}
