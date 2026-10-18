// Copyright 2026 MinLZ C contributors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"time"

	"github.com/minio/minlz"
)

const indexFindInnerLoops = 2048

func die(err error) {
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
}

func parseLevel(args []string) int {
	if len(args) == 0 {
		return minlz.LevelBalanced
	}
	level, err := strconv.Atoi(args[0])
	die(err)
	switch level {
	case minlz.LevelSuperFast, minlz.LevelUncompressed, minlz.LevelFastest, minlz.LevelBalanced, minlz.LevelSmallest:
		return level
	default:
		die(fmt.Errorf("invalid level %d", level))
	}
	return minlz.LevelBalanced
}

func parsePositiveInt(s string) int {
	n, err := strconv.Atoi(s)
	die(err)
	if n <= 0 {
		die(fmt.Errorf("value must be positive: %s", s))
	}
	return n
}

func mbps(bytes int, loops int, elapsed time.Duration) float64 {
	secs := elapsed.Seconds()
	if secs <= 0 {
		secs = 1e-9
	}
	return (float64(bytes) * float64(loops) / (1024.0 * 1024.0)) / secs
}

func printPerfRow(kind, name string, level int, rawLen, encLen int, samples []float64) {
	sort.Float64s(samples)
	ratio := 0.0
	if rawLen != 0 {
		ratio = float64(encLen) / float64(rawLen)
	}
	fmt.Printf("%s,%s,%d,%d,%d,%.6f,%.2f,%.2f,%.2f\n",
		kind, name, level, rawLen, encLen, ratio,
		samples[0], samples[len(samples)/2], samples[len(samples)-1])
}

func embeddedIndexLen(enc []byte) int {
	if len(enc) < 10 {
		return 0
	}
	if !bytes.Equal(enc[len(enc)-6:], []byte("\x00xdi2s")) {
		return 0
	}
	n := int(binary.LittleEndian.Uint32(enc[len(enc)-10:]))
	if n < 20 || n > len(enc) {
		return 0
	}
	return n
}

func benchBlock(name string, in []byte, level, loops, samples int) {
	encSamples := make([]float64, samples)
	decSamples := make([]float64, samples)
	var enc []byte
	for sample := 0; sample < samples; sample++ {
		start := time.Now()
		for i := 0; i < loops; i++ {
			var err error
			enc, err = minlz.Encode(enc[:0], in, level)
			die(err)
		}
		encSamples[sample] = mbps(len(in), loops, time.Since(start))
	}
	dec := make([]byte, 0, len(in))
	validateDecodeOutsideLoop := len(in) >= 1<<20 || (len(enc) >= 2 && enc[0] == 0 && enc[1] == 0)
	for sample := 0; sample < samples; sample++ {
		start := time.Now()
		for i := 0; i < loops; i++ {
			var err error
			dec, err = minlz.Decode(dec[:0], enc)
			die(err)
			if !validateDecodeOutsideLoop && !bytes.Equal(dec, in) {
				die(fmt.Errorf("Go decode mismatch"))
			}
		}
		decSamples[sample] = mbps(len(in), loops, time.Since(start))
		if validateDecodeOutsideLoop && !bytes.Equal(dec, in) {
			die(fmt.Errorf("Go decode mismatch"))
		}
	}
	printPerfRow("go-block-encode", name, level, len(in), len(enc), encSamples)
	printPerfRow("go-block-decode", name, level, len(in), len(enc), decSamples)
}

func benchCopy(name string, in []byte, loops, samples int) {
	encSamples := make([]float64, samples)
	decSamples := make([]float64, samples)
	enc := make([]byte, len(in)+2)
	dec := make([]byte, len(in))
	enc[0] = 0
	enc[1] = 0
	copy(enc[2:], in)
	for sample := 0; sample < samples; sample++ {
		start := time.Now()
		for i := 0; i < loops; i++ {
			enc[0] = 0
			enc[1] = 0
			copy(enc[2:], in)
		}
		encSamples[sample] = mbps(len(in), loops, time.Since(start))
	}
	for sample := 0; sample < samples; sample++ {
		start := time.Now()
		for i := 0; i < loops; i++ {
			copy(dec, enc[2:])
		}
		decSamples[sample] = mbps(len(in), loops, time.Since(start))
	}
	if !bytes.Equal(dec, in) {
		die(fmt.Errorf("Go copy decode mismatch"))
	}
	printPerfRow("go-copy-encode", name, minlz.LevelUncompressed, len(in), len(enc), encSamples)
	printPerfRow("go-copy-decode", name, minlz.LevelUncompressed, len(in), len(enc), decSamples)
}

func benchStream(name string, in []byte, level, loops, samples int) {
	encSamples := make([]float64, samples)
	decSamples := make([]float64, samples)
	var enc []byte
	for sample := 0; sample < samples; sample++ {
		start := time.Now()
		for i := 0; i < loops; i++ {
			var buf bytes.Buffer
			w := minlz.NewWriter(&buf, minlz.WriterBlockSize(8<<20), minlz.WriterLevel(level), minlz.WriterAddIndex(true))
			_, err := w.Write(in)
			die(err)
			die(w.Close())
			enc = buf.Bytes()
		}
		encSamples[sample] = mbps(len(in), loops, time.Since(start))
	}
	for sample := 0; sample < samples; sample++ {
		start := time.Now()
		for i := 0; i < loops; i++ {
			r := minlz.NewReader(bytes.NewReader(enc), minlz.ReaderFallback(true))
			dec, err := io.ReadAll(r)
			die(err)
			if !bytes.Equal(dec, in) {
				die(fmt.Errorf("Go stream decode mismatch"))
			}
		}
		decSamples[sample] = mbps(len(in), loops, time.Since(start))
	}
	printPerfRow("go-stream-encode", name, level, len(in), len(enc), encSamples)
	printPerfRow("go-stream-decode", name, level, len(in), len(enc), decSamples)
}

func benchStreamConcurrent(name string, in []byte, level, threads, loops, samples int) {
	var enc []byte
	{
		var buf bytes.Buffer
		w := minlz.NewWriter(&buf, minlz.WriterBlockSize(8<<20), minlz.WriterLevel(level), minlz.WriterAddIndex(true))
		_, err := w.Write(in)
		die(err)
		die(w.Close())
		enc = append(enc[:0], buf.Bytes()...)
	}
	{
		var buf bytes.Buffer
		r := minlz.NewReader(bytes.NewReader(enc), minlz.ReaderFallback(true))
		n, err := r.DecodeConcurrent(&buf, threads)
		die(err)
		if n != int64(len(in)) || !bytes.Equal(buf.Bytes(), in) {
			die(fmt.Errorf("Go DecodeConcurrent mismatch"))
		}
	}
	decSamples := make([]float64, samples)
	for sample := 0; sample < samples; sample++ {
		start := time.Now()
		for i := 0; i < loops; i++ {
			r := minlz.NewReader(bytes.NewReader(enc), minlz.ReaderFallback(true))
			n, err := r.DecodeConcurrent(io.Discard, threads)
			die(err)
			if n != int64(len(in)) {
				die(fmt.Errorf("Go DecodeConcurrent size mismatch"))
			}
		}
		decSamples[sample] = mbps(len(in), loops, time.Since(start))
	}
	printPerfRow(fmt.Sprintf("go-stream-decode-concurrent-%d", threads),
		name, level, len(in), len(enc), decSamples)
}

func benchIndex(name string, in []byte, level, loops, samples int) {
	var enc []byte
	var encNoIndex []byte
	{
		var buf bytes.Buffer
		w := minlz.NewWriter(&buf, minlz.WriterBlockSize(8<<20), minlz.WriterLevel(level), minlz.WriterAddIndex(true))
		_, err := w.Write(in)
		die(err)
		die(w.Close())
		enc = append(enc[:0], buf.Bytes()...)
	}
	{
		var buf bytes.Buffer
		w := minlz.NewWriter(&buf, minlz.WriterBlockSize(8<<20), minlz.WriterLevel(level), minlz.WriterAddIndex(false))
		_, err := w.Write(in)
		die(err)
		die(w.Close())
		encNoIndex = append(encNoIndex[:0], buf.Bytes()...)
	}

	idxBuildSamples := make([]float64, samples)
	idxLoadSamples := make([]float64, samples)
	idxFindSamples := make([]float64, samples)
	readAtSamples := make([]float64, samples)

	var idxBytes []byte
	embeddedIdxLen := embeddedIndexLen(enc)
	idxLen := embeddedIdxLen
	indexStreamOK := true
	for sample := 0; sample < samples; sample++ {
		if !indexStreamOK {
			continue
		}
		start := time.Now()
		for i := 0; i < loops; i++ {
			var err error
			idxBytes, err = minlz.IndexStream(bytes.NewReader(encNoIndex))
			if err != nil {
				fmt.Fprintf(os.Stderr, "Go IndexStream failed on %s level %d: %v\n", name, level, err)
				idxBytes = nil
				indexStreamOK = false
				break
			}
		}
		if len(idxBytes) != 0 {
			idxBuildSamples[sample] = mbps(len(in), loops, time.Since(start))
		}
	}
	if len(idxBytes) != 0 {
		idxLen = len(idxBytes)
	}
	printPerfRow("go-index-build", name, level, len(in), idxLen, idxBuildSamples)

	for sample := 0; sample < samples; sample++ {
		start := time.Now()
		for i := 0; i < loops; i++ {
			var idx minlz.Index
			if err := idx.LoadStream(bytes.NewReader(enc)); err != nil {
				die(fmt.Errorf("Go Index.LoadStream: %w", err))
			}
		}
		idxLoadSamples[sample] = mbps(len(in), loops, time.Since(start))
	}
	printPerfRow("go-index-load", name, level, len(in), embeddedIdxLen, idxLoadSamples)

	var idx minlz.Index
	if err := idx.LoadStream(bytes.NewReader(enc)); err != nil {
		die(fmt.Errorf("Go Index.LoadStream setup: %w", err))
	}
	for sample := 0; sample < samples; sample++ {
		start := time.Now()
		for i := 0; i < loops*indexFindInnerLoops; i++ {
			off := int64(0)
			if len(in) != 0 {
				off = int64((uint64(i) * 104729) % uint64(len(in)))
			}
			_, _, err := idx.Find(off)
			if err != nil {
				die(fmt.Errorf("Go Index.Find: %w", err))
			}
		}
		idxFindSamples[sample] = mbps(len(in)*indexFindInnerLoops, loops, time.Since(start))
	}
	printPerfRow("go-index-find", name, level, len(in), embeddedIdxLen, idxFindSamples)

	tmp := make([]byte, 256)
	readLen := len(tmp)
	if len(in) < readLen {
		readLen = len(in)
	}
	for sample := 0; sample < samples; sample++ {
		r := minlz.NewReader(bytes.NewReader(enc), minlz.ReaderFallback(true))
		rs, err := r.ReadSeeker(nil)
		if err != nil {
			die(fmt.Errorf("Go Reader.ReadSeeker: %w", err))
		}
		n, err := rs.ReadAt(tmp[:readLen], 0)
		if err != nil && err != io.EOF {
			die(fmt.Errorf("Go ReadAt validation: %w", err))
		}
		if n != readLen || !bytes.Equal(tmp[:readLen], in[:readLen]) {
			die(fmt.Errorf("Go ReadAt validation mismatch"))
		}
		start := time.Now()
		for i := 0; i < loops*16; i++ {
			off := int64(0)
			if len(in) > readLen {
				off = int64((uint64(i) * 65537) % uint64(len(in)-readLen))
			}
			n, err = rs.ReadAt(tmp[:readLen], off)
			if err != nil && err != io.EOF {
				die(fmt.Errorf("Go ReadAt: %w", err))
			}
			if n != readLen || !bytes.Equal(tmp[:readLen], in[off:int(off)+readLen]) {
				die(fmt.Errorf("Go ReadAt mismatch"))
			}
		}
		readAtSamples[sample] = mbps(readLen*16, loops, time.Since(start))
	}
	printPerfRow("go-read-at-hot", name, level, len(in), len(enc), readAtSamples)
}

func huff0SearchFixture() []byte {
	const (
		size         = 1 << 20
		needleOffset = 123456
	)
	out := make([]byte, size)
	state := uint32(0x9e3779b9)
	for i := 0; i < len(out); i += 8 {
		out[i] = ':'
		for j := 1; j < 8 && i+j < len(out); j++ {
			state ^= state << 13
			state ^= state >> 17
			state ^= state << 5
			out[i+j] = byte('a' + state%26)
		}
	}
	copy(out[needleOffset:], []byte(":HUFF0-NEEDLE"))
	return out
}

func streamHasHuff0SearchTable(stream []byte) bool {
	for len(stream) >= 4 {
		n := int(stream[1]) | int(stream[2])<<8 | int(stream[3])<<16
		if n > len(stream)-4 {
			return false
		}
		if stream[0] == 0x46 {
			payload := stream[4 : 4+n]
			if len(payload) < 10 {
				return false
			}
			prefixSize := 0
			switch payload[0] {
			case 1:
			case 2:
				prefixSize = 8
			case 3:
				prefixSize = 32
			case 4:
				if len(payload) < 5 {
					return false
				}
				prefixSize = 2 + int(payload[3]) + 1
			default:
				return false
			}
			tableCountAt := 3 + prefixSize + 1 + 4 + 1
			if tableCountAt < len(payload) &&
				payload[tableCountAt] != 0 {
				return true
			}
		}
		stream = stream[4+n:]
	}
	return false
}

type searchMatrixCase struct {
	name    string
	cfg     minlz.SearchTableConfig
	cliArgs []string
	flush   bool
}

func searchOffsetsNaive(data, pattern []byte) []int64 {
	var out []int64
	for base := 0; base <= len(data)-len(pattern); {
		at := bytes.Index(data[base:], pattern)
		if at < 0 {
			break
		}
		base += at
		out = append(out, int64(base))
		base++
	}
	return out
}

func searchOffsetsGo(stream, pattern []byte) ([]int64, error) {
	var out []int64
	searcher := minlz.NewBlockSearcher(bytes.NewReader(stream))
	err := searcher.Search(pattern, func(result minlz.SearchResult) error {
		out = append(out, result.StreamOffset)
		return nil
	})
	return out, err
}

func searchOffsetsC(mz, streamPath string, pattern []byte) ([]int64, error) {
	cmd := exec.Command(mz, "search", "-offsets", string(pattern), streamPath)
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	err := cmd.Run()
	if err != nil {
		// mz follows grep convention: exit 1 means a valid search with no
		// matches, while exit >=2 is an actual failure.
		if exitErr, ok := err.(*exec.ExitError); !ok || exitErr.ExitCode() != 1 {
			return nil, fmt.Errorf("C search failed: %w: %s", err, stderr.Bytes())
		}
	}
	fields := bytes.Fields(stdout.Bytes())
	out := make([]int64, 0, len(fields))
	for _, field := range fields {
		offset, err := strconv.ParseInt(string(field), 10, 64)
		if err != nil {
			return nil, fmt.Errorf("bad C search offset %q: %w", field, err)
		}
		out = append(out, offset)
	}
	return out, nil
}

func equalOffsets(got, want []int64) bool {
	if len(got) != len(want) {
		return false
	}
	for i := range got {
		if got[i] != want[i] {
			return false
		}
	}
	return true
}

func writeGoSearchMatrixStream(path string, data []byte, tc searchMatrixCase) error {
	var stream bytes.Buffer
	writer := minlz.NewWriter(
		&stream,
		minlz.WriterBlockSize(4<<10),
		minlz.WriterLevel(minlz.LevelBalanced),
		minlz.WriterConcurrency(1),
		minlz.WriterSearchTable(tc.cfg),
		minlz.WriterAddIndex(false),
	)
	if tc.flush {
		cut := 4<<10 - 31
		if _, err := writer.Write(data[:cut]); err != nil {
			return err
		}
		if err := writer.Flush(); err != nil {
			return err
		}
		if _, err := writer.Write(data[cut:]); err != nil {
			return err
		}
	} else {
		for pos := 0; pos < len(data); {
			n := 137 + (pos*17)%5003
			if n > len(data)-pos {
				n = len(data) - pos
			}
			if _, err := writer.Write(data[pos : pos+n]); err != nil {
				return err
			}
			pos += n
		}
	}
	if err := writer.Close(); err != nil {
		return err
	}
	return os.WriteFile(path, stream.Bytes(), 0o644)
}

func writeCSearchMatrixStream(mz, inputPath, outputPath string, tc searchMatrixCase) error {
	args := []string{
		"c", "-q", "-2", "-bs", "4096", "-no-index",
		"-search", "-search.len", strconv.Itoa(matrixMatchLen(tc.cfg)),
	}
	args = append(args, tc.cliArgs...)
	args = append(args, "-o", outputPath, inputPath)
	cmd := exec.Command(mz, args...)
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("C search encode failed: %w: %s", err, output)
	}
	return nil
}

func matrixMatchLen(cfg minlz.SearchTableConfig) int {
	// SearchTableConfig intentionally hides its fields. Its stable String
	// representation starts with "matchLen=N".
	var n int
	if _, err := fmt.Sscanf(cfg.String(), "matchLen=%d", &n); err != nil {
		panic(err)
	}
	return n
}

func verifySearchMatrix(mz, workDir string) error {
	const blockSize = 4 << 10
	if err := os.MkdirAll(workDir, 0o755); err != nil {
		return err
	}
	data := make([]byte, blockSize*4+777)
	state := uint32(0x6d2b79f5)
	alphabet := []byte("abcdefghijklmnopqrstuvwxyz0123456789=:,;.[{( ")
	for i := range data {
		state ^= state << 13
		state ^= state >> 17
		state ^= state << 5
		data[i] = alphabet[int(state)%len(alphabet)]
	}
	pattern := []byte("KEY=boundary-search-target")
	for _, offset := range []int{73, blockSize - 3, 2*blockSize + 111, 4*blockSize - 9} {
		copy(data[offset:], pattern)
	}
	absent := []byte("KEY=definitely-not-present")
	inputPath := filepath.Join(workDir, "search-matrix.raw")
	if err := os.WriteFile(inputPath, data, 0o644); err != nil {
		return err
	}

	maskPrefixes := "=:,;.[{("
	cases := []searchMatrixCase{
		{
			name: "none-m1-uncompressed",
			cfg: minlz.NewSearchTableConfig().
				WithMatchLen(1).
				WithoutCompression(),
			cliArgs: []string{"-search.uncompressed"},
		},
		{
			name: "none-m8-compressed",
			cfg:  minlz.NewSearchTableConfig().WithMatchLen(8),
		},
		{
			name: "byte-m4-compressed",
			cfg: minlz.NewSearchTableConfig().
				WithMatchLen(4).
				WithBytePrefix('=', ':'),
			cliArgs: []string{"-search.prefixes", "=:"},
		},
		{
			name: "byte-m6-uncompressed-flush",
			cfg: minlz.NewSearchTableConfig().
				WithMatchLen(6).
				WithBytePrefix('=', ':').
				WithoutCompression(),
			cliArgs: []string{
				"-search.prefixes", "=:",
				"-search.uncompressed",
			},
			flush: true,
		},
		{
			name: "mask-m6-compressed",
			cfg: func() minlz.SearchTableConfig {
				var mask [32]byte
				for _, value := range []byte(maskPrefixes) {
					mask[value>>3] |= 1 << (value & 7)
				}
				return minlz.NewSearchTableConfig().
					WithMatchLen(6).
					WithMaskPrefix(mask)
			}(),
			cliArgs: []string{"-search.prefixes", maskPrefixes},
		},
		{
			name: "long-m4-e0",
			cfg: minlz.NewSearchTableConfig().
				WithMatchLen(4).
				WithLongPrefix([]byte("KEY=")),
			cliArgs: []string{"-search.prefix", "KEY="},
		},
		{
			name: "long-m4-e3-compressed",
			cfg: minlz.NewSearchTableConfig().
				WithMatchLen(4).
				WithLongPrefix([]byte("KEY=")).
				WithExtras(3),
			cliArgs: []string{
				"-search.prefix", "KEY=",
				"-search.extras", "3",
			},
		},
		{
			name: "long-m8-e8-uncompressed",
			cfg: minlz.NewSearchTableConfig().
				WithMatchLen(8).
				WithLongPrefix([]byte("KEY=")).
				WithExtras(8).
				WithoutCompression(),
			cliArgs: []string{
				"-search.prefix", "KEY=",
				"-search.extras", "8",
				"-search.uncompressed",
			},
		},
	}

	for _, tc := range cases {
		goPath := filepath.Join(workDir, tc.name+"-go.mz")
		cPath := filepath.Join(workDir, tc.name+"-c.mz")
		if err := writeGoSearchMatrixStream(goPath, data, tc); err != nil {
			return fmt.Errorf("%s Go encode: %w", tc.name, err)
		}
		if err := writeCSearchMatrixStream(mz, inputPath, cPath, tc); err != nil {
			return fmt.Errorf("%s C encode: %w", tc.name, err)
		}
		cStream, err := os.ReadFile(cPath)
		if err != nil {
			return err
		}
		for _, query := range [][]byte{pattern, absent} {
			want := searchOffsetsNaive(data, query)
			gotC, err := searchOffsetsC(mz, goPath, query)
			if err != nil {
				return fmt.Errorf("%s C search: %w", tc.name, err)
			}
			if !equalOffsets(gotC, want) {
				return fmt.Errorf("%s C search %q: got %v, want %v",
					tc.name, query, gotC, want)
			}
			gotGo, err := searchOffsetsGo(cStream, query)
			if err != nil {
				return fmt.Errorf("%s Go search: %w", tc.name, err)
			}
			if !equalOffsets(gotGo, want) {
				return fmt.Errorf("%s Go search %q: got %v, want %v",
					tc.name, query, gotGo, want)
			}
		}
	}
	fmt.Printf("search matrix: %d configs, Go->C and C->Go OK\n", len(cases))
	return nil
}

func main() {
	if len(os.Args) < 4 {
		die(fmt.Errorf("usage: minlz-go-compat <encode-block|decode-block|encode-stream|decode-stream|encode-search-stream|encode-search-stream-compressed|encode-search-config|encode-search-huff0-stream|encode-search-prefix-only|assert-huff0-stream|search-stream|build-sidecar|build-sidecar-prefix-only|extract-sidecar|search-sidecar|verify-search-matrix|bench-block|bench-copy|bench-stream|bench-stream-concurrent|bench-index> <in> <out-or-name> [args]"))
	}
	in, err := os.ReadFile(os.Args[2])
	die(err)
	switch os.Args[1] {
	case "encode-block":
		if len(os.Args) > 5 {
			die(fmt.Errorf("usage: minlz-go-compat encode-block <in> <out> [level]"))
		}
		out, err := minlz.Encode(nil, in, parseLevel(os.Args[4:]))
		die(err)
		die(os.WriteFile(os.Args[3], out, 0644))
	case "decode-block":
		if len(os.Args) != 4 {
			die(fmt.Errorf("usage: minlz-go-compat decode-block <in> <out>"))
		}
		out, err := minlz.Decode(nil, in)
		die(err)
		die(os.WriteFile(os.Args[3], out, 0644))
	case "encode-stream":
		if len(os.Args) > 5 {
			die(fmt.Errorf("usage: minlz-go-compat encode-stream <in> <out> [level]"))
		}
		var buf bytes.Buffer
		w := minlz.NewWriter(&buf, minlz.WriterBlockSize(8<<20), minlz.WriterLevel(parseLevel(os.Args[4:])), minlz.WriterAddIndex(true))
		_, err := w.Write(in)
		die(err)
		die(w.Close())
		die(os.WriteFile(os.Args[3], buf.Bytes(), 0644))
	case "decode-stream":
		if len(os.Args) != 4 {
			die(fmt.Errorf("usage: minlz-go-compat decode-stream <in> <out>"))
		}
		r := minlz.NewReader(bytes.NewReader(in), minlz.ReaderFallback(true))
		out, err := io.ReadAll(r)
		die(err)
		die(os.WriteFile(os.Args[3], out, 0644))
	case "encode-search-stream":
		if len(os.Args) > 5 {
			die(fmt.Errorf("usage: minlz-go-compat encode-search-stream <in> <out> [level]"))
		}
		var buf bytes.Buffer
		cfg := minlz.NewSearchTableConfig().WithoutCompression()
		w := minlz.NewWriter(&buf,
			minlz.WriterBlockSize(4<<10),
			minlz.WriterLevel(parseLevel(os.Args[4:])),
			minlz.WriterConcurrency(1),
			minlz.WriterSearchTable(cfg),
			minlz.WriterAddIndex(true))
		_, err := w.Write(in)
		die(err)
		die(w.Close())
		die(os.WriteFile(os.Args[3], buf.Bytes(), 0644))
	case "encode-search-stream-compressed":
		if len(os.Args) > 5 {
			die(fmt.Errorf("usage: minlz-go-compat encode-search-stream-compressed <in> <out> [level]"))
		}
		var buf bytes.Buffer
		w := minlz.NewWriter(&buf,
			minlz.WriterBlockSize(4<<10),
			minlz.WriterLevel(parseLevel(os.Args[4:])),
			minlz.WriterConcurrency(1),
			minlz.WriterSearchTable(minlz.NewSearchTableConfig()),
			minlz.WriterAddIndex(true))
		_, err := w.Write(in)
		die(err)
		die(w.Close())
		die(os.WriteFile(os.Args[3], buf.Bytes(), 0644))
	case "encode-search-config":
		if len(os.Args) < 7 || len(os.Args) > 9 {
			die(fmt.Errorf("usage: minlz-go-compat encode-search-config <in> <out> <none|byte|mask|long> <match-len> <prefix> [extras] [level]"))
		}
		matchLen := parsePositiveInt(os.Args[5])
		cfg := minlz.NewSearchTableConfig().WithMatchLen(matchLen)
		switch os.Args[4] {
		case "none":
		case "byte":
			cfg = cfg.WithBytePrefix([]byte(os.Args[6])...)
		case "mask":
			var mask [32]byte
			for _, value := range []byte(os.Args[6]) {
				mask[value>>3] |= 1 << (value & 7)
			}
			cfg = cfg.WithMaskPrefix(mask)
		case "long":
			cfg = cfg.WithLongPrefix([]byte(os.Args[6]))
			if len(os.Args) >= 8 {
				extras := parsePositiveInt(os.Args[7])
				cfg = cfg.WithExtras(extras)
			}
		default:
			die(fmt.Errorf("unknown search config kind %q", os.Args[4]))
		}
		levelArgs := []string(nil)
		if len(os.Args) == 9 {
			levelArgs = os.Args[8:9]
		} else if len(os.Args) == 8 && os.Args[4] != "long" {
			levelArgs = os.Args[7:8]
		}
		var buf bytes.Buffer
		w := minlz.NewWriter(&buf,
			minlz.WriterBlockSize(4<<10),
			minlz.WriterLevel(parseLevel(levelArgs)),
			minlz.WriterConcurrency(1),
			minlz.WriterSearchTable(cfg),
			minlz.WriterAddIndex(true))
		_, err := w.Write(in)
		die(err)
		die(w.Close())
		die(os.WriteFile(os.Args[3], buf.Bytes(), 0644))
	case "encode-search-huff0-stream":
		if len(os.Args) != 4 {
			die(fmt.Errorf("usage: minlz-go-compat encode-search-huff0-stream <ignored-input> <out>"))
		}
		in = huff0SearchFixture()
		var buf bytes.Buffer
		cfg := minlz.NewSearchTableConfig().
			WithMatchLen(6).
			WithBytePrefix(':').
			WithCompression(minlz.CompressedSearchForce())
		w := minlz.NewWriter(&buf,
			minlz.WriterBlockSize(1<<20),
			minlz.WriterLevel(minlz.LevelBalanced),
			minlz.WriterConcurrency(1),
			minlz.WriterSearchTable(cfg),
			minlz.WriterAddIndex(true))
		_, err := w.Write(in)
		die(err)
		die(w.Close())
		if !streamHasHuff0SearchTable(buf.Bytes()) {
			die(fmt.Errorf("fixture did not produce a Huff0-tabled 0x46 chunk"))
		}
		die(os.WriteFile(os.Args[3], buf.Bytes(), 0644))
	case "encode-search-prefix-only":
		if len(os.Args) < 5 || len(os.Args) > 6 {
			die(fmt.Errorf("usage: minlz-go-compat encode-search-prefix-only <in> <out> <prefix> [level]"))
		}
		var buf bytes.Buffer
		cfg := minlz.NewSearchTableConfig().
			WithLongPrefix([]byte(os.Args[4])).
			WithoutCompression()
		w := minlz.NewWriter(&buf,
			minlz.WriterBlockSize(4<<10),
			minlz.WriterLevel(parseLevel(os.Args[5:])),
			minlz.WriterConcurrency(1),
			minlz.WriterSearchTable(cfg),
			minlz.WriterAddIndex(true))
		_, err := w.Write(in)
		die(err)
		die(w.Close())
		die(os.WriteFile(os.Args[3], buf.Bytes(), 0644))
	case "assert-huff0-stream":
		if len(os.Args) != 4 {
			die(fmt.Errorf("usage: minlz-go-compat assert-huff0-stream <in> <ignored-out>"))
		}
		if !streamHasHuff0SearchTable(in) {
			die(fmt.Errorf("stream has no Huff0-tabled 0x46 chunk"))
		}
	case "search-stream":
		if len(os.Args) < 5 || len(os.Args) > 6 {
			die(fmt.Errorf("usage: minlz-go-compat search-stream <in> <out> <pattern> [bail]"))
		}
		var out bytes.Buffer
		var opts []minlz.BlockSearchOption
		if len(os.Args) == 6 && os.Args[5] == "bail" {
			opts = append(opts, minlz.BlockSearchBailOnMissing())
		}
		searcher := minlz.NewBlockSearcher(bytes.NewReader(in), opts...)
		err := searcher.Search([]byte(os.Args[4]), func(result minlz.SearchResult) error {
			_, err := fmt.Fprintln(&out, result.StreamOffset)
			return err
		})
		die(err)
		die(os.WriteFile(os.Args[3], out.Bytes(), 0644))
	case "build-sidecar":
		if len(os.Args) != 4 {
			die(fmt.Errorf("usage: minlz-go-compat build-sidecar <in> <out>"))
		}
		var sidecar bytes.Buffer
		die(minlz.BuildSidecar(
			&sidecar, bytes.NewReader(in),
			minlz.SidecarSearchTable(minlz.NewSearchTableConfig())))
		die(os.WriteFile(os.Args[3], sidecar.Bytes(), 0644))
	case "build-sidecar-prefix-only":
		if len(os.Args) != 5 {
			die(fmt.Errorf("usage: minlz-go-compat build-sidecar-prefix-only <in> <out> <prefix>"))
		}
		var sidecar bytes.Buffer
		cfg := minlz.NewSearchTableConfig().
			WithLongPrefix([]byte(os.Args[4]))
		die(minlz.BuildSidecar(
			&sidecar, bytes.NewReader(in),
			minlz.SidecarSearchTable(cfg)))
		die(os.WriteFile(os.Args[3], sidecar.Bytes(), 0644))
	case "extract-sidecar":
		if len(os.Args) != 5 {
			die(fmt.Errorf("usage: minlz-go-compat extract-sidecar <in> <sidecar-out> <stripped-out>"))
		}
		var sidecar, stripped bytes.Buffer
		die(minlz.ExtractSidecar(
			&sidecar, &stripped, bytes.NewReader(in)))
		die(os.WriteFile(os.Args[3], sidecar.Bytes(), 0644))
		die(os.WriteFile(os.Args[4], stripped.Bytes(), 0644))
	case "search-sidecar":
		if len(os.Args) != 6 {
			die(fmt.Errorf("usage: minlz-go-compat search-sidecar <main> <out> <sidecar> <pattern>"))
		}
		sidecar, err := os.ReadFile(os.Args[4])
		die(err)
		var out bytes.Buffer
		searcher := minlz.NewSidecarSearcher(
			bytes.NewReader(in), bytes.NewReader(sidecar))
		err = searcher.Search([]byte(os.Args[5]), func(result minlz.SearchResult) error {
			_, err := fmt.Fprintln(&out, result.StreamOffset)
			return err
		})
		die(err)
		die(os.WriteFile(os.Args[3], out.Bytes(), 0644))
	case "verify-search-matrix":
		if len(os.Args) != 5 {
			die(fmt.Errorf("usage: minlz-go-compat verify-search-matrix <ignored-input> <mz> <work-dir>"))
		}
		die(verifySearchMatrix(os.Args[3], os.Args[4]))
	case "bench-block":
		if len(os.Args) != 7 {
			die(fmt.Errorf("usage: minlz-go-compat bench-block <in> <name> <level> <loops> <samples>"))
		}
		benchBlock(os.Args[3], in, parseLevel(os.Args[4:5]),
			parsePositiveInt(os.Args[5]), parsePositiveInt(os.Args[6]))
	case "bench-copy":
		if len(os.Args) != 6 {
			die(fmt.Errorf("usage: minlz-go-compat bench-copy <in> <name> <loops> <samples>"))
		}
		benchCopy(os.Args[3], in, parsePositiveInt(os.Args[4]), parsePositiveInt(os.Args[5]))
	case "bench-stream":
		if len(os.Args) != 7 {
			die(fmt.Errorf("usage: minlz-go-compat bench-stream <in> <name> <level> <loops> <samples>"))
		}
		benchStream(os.Args[3], in, parseLevel(os.Args[4:5]),
			parsePositiveInt(os.Args[5]), parsePositiveInt(os.Args[6]))
	case "bench-stream-concurrent":
		if len(os.Args) != 8 {
			die(fmt.Errorf("usage: minlz-go-compat bench-stream-concurrent <in> <name> <level> <threads> <loops> <samples>"))
		}
		benchStreamConcurrent(os.Args[3], in, parseLevel(os.Args[4:5]),
			parsePositiveInt(os.Args[5]), parsePositiveInt(os.Args[6]),
			parsePositiveInt(os.Args[7]))
	case "bench-index":
		if len(os.Args) != 7 {
			die(fmt.Errorf("usage: minlz-go-compat bench-index <in> <name> <level> <loops> <samples>"))
		}
		benchIndex(os.Args[3], in, parseLevel(os.Args[4:5]),
			parsePositiveInt(os.Args[5]), parsePositiveInt(os.Args[6]))
	default:
		die(fmt.Errorf("unknown command %q", os.Args[1]))
	}
}
