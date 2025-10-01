package main

import (
	"bufio"
	"context"
	"embed"
	"encoding/json"
	"flag"
	"fmt"
	"html/template"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"sync"
	"syscall"
	"time"
)

// Version information
const (
	Version = "1.0.0"
	Author  = "github.com/suifei"
)

// Default configuration values
const (
	DefaultPort     = 11980
	DefaultBind     = "0.0.0.0"
	DefaultInterval = 3
)

//go:embed static/*
var staticFiles embed.FS

// Config holds application configuration
type Config struct {
	Port          int
	BindAddress   string
	PollInterval  time.Duration
	ConfigPath    string
	VerboseLog    bool
	BoundDevices  []string
}

// Device represents a USB device
type Device struct {
	BusID string `json:"busid"`
	Info  string `json:"info"`
	Bound bool   `json:"bound"`
}

// Server manages the HTTP server and device state
type Server struct {
	config   *Config
	devices  []Device
	mu       sync.RWMutex
	clients  map[chan []Device]struct{}
	clientMu sync.Mutex
	logger   *log.Logger
}

// NewServer creates a new server instance
func NewServer(cfg *Config) *Server {
	logger := log.New(os.Stdout, "", log.LstdFlags)
	if !cfg.VerboseLog {
		logger.SetOutput(io.Discard)
	}

	return &Server{
		config:  cfg,
		devices: []Device{},
		clients: make(map[chan []Device]struct{}),
		logger:  logger,
	}
}

// loadConfig loads configuration from file
func loadConfig(path string) (*Config, error) {
	cfg := &Config{
		Port:         DefaultPort,
		BindAddress:  DefaultBind,
		PollInterval: DefaultInterval * time.Second,
	}

	if path == "" {
		return cfg, nil
	}

	file, err := os.Open(path)
	if err != nil {
		if os.IsNotExist(err) {
			return cfg, nil // Config file doesn't exist, use defaults
		}
		return nil, fmt.Errorf("failed to open config: %w", err)
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			continue
		}

		key, value := strings.TrimSpace(parts[0]), strings.TrimSpace(parts[1])
		switch key {
		case "port":
			fmt.Sscanf(value, "%d", &cfg.Port)
		case "bind":
			cfg.BindAddress = value
		case "poll_interval":
			var interval int
			fmt.Sscanf(value, "%d", &interval)
			cfg.PollInterval = time.Duration(interval) * time.Second
		case "bound_device":
			cfg.BoundDevices = append(cfg.BoundDevices, value)
		}
	}

	return cfg, scanner.Err()
}

// saveConfig saves configuration to file
func (s *Server) saveConfig() error {
	if s.config.ConfigPath == "" {
		return nil
	}

	dir := filepath.Dir(s.config.ConfigPath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("failed to create config directory: %w", err)
	}

	file, err := os.Create(s.config.ConfigPath)
	if err != nil {
		return fmt.Errorf("failed to create config file: %w", err)
	}
	defer file.Close()

	fmt.Fprintf(file, "port=%d\n", s.config.Port)
	fmt.Fprintf(file, "bind=%s\n", s.config.BindAddress)
	fmt.Fprintf(file, "poll_interval=%d\n", int(s.config.PollInterval.Seconds()))

	s.mu.RLock()
	for _, dev := range s.devices {
		if dev.Bound {
			fmt.Fprintf(file, "bound_device=%s\n", dev.BusID)
		}
	}
	s.mu.RUnlock()

	return nil
}

// validateBusID checks if a bus ID is valid
func validateBusID(busid string) bool {
	// Bus ID format: number-number.number (e.g., "1-1.2")
	match, _ := regexp.MatchString(`^[0-9]+-[0-9]+(\.[0-9]+)*$`, busid)
	return match
}

// execCommand safely executes a command with validation
func execCommand(name string, args ...string) (string, error) {
	// Whitelist of allowed commands
	allowed := map[string]bool{
		"usbip":   true,
		"lsusb":   true,
		"usbipd":  true,
	}

	if !allowed[name] {
		return "", fmt.Errorf("command not allowed: %s", name)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	cmd := exec.CommandContext(ctx, name, args...)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return string(output), fmt.Errorf("command failed: %w: %s", err, output)
	}

	return string(output), nil
}

// isDeviceBound checks if a device is bound to usbip-host
func isDeviceBound(busid string) bool {
	if runtime.GOOS == "windows" {
		// On Windows, check via usbipd command
		return false // Simplified for now
	}

	path := filepath.Join("/sys/bus/usb/drivers/usbip-host", busid)
	_, err := os.Stat(path)
	return err == nil
}

// listDevices retrieves list of USB devices
func (s *Server) listDevices() ([]Device, error) {
	var cmd string
	var args []string

	if runtime.GOOS == "windows" {
		cmd = "usbipd"
		args = []string{"wsl", "list"}
	} else {
		cmd = "usbip"
		args = []string{"list", "-l"}
	}

	output, err := execCommand(cmd, args...)
	if err != nil {
		return nil, err
	}

	return s.parseDevices(output), nil
}

// parseDevices parses usbip list output
func (s *Server) parseDevices(output string) []Device {
	var devices []Device
	var current *Device

	scanner := bufio.NewScanner(strings.NewReader(output))
	for scanner.Scan() {
		line := scanner.Text()
		trimmed := strings.TrimSpace(line)

		if trimmed == "" {
			continue
		}

		// New device line
		if strings.HasPrefix(trimmed, "- busid") || strings.HasPrefix(trimmed, "BUSID") {
			if current != nil {
				devices = append(devices, *current)
			}

			current = &Device{}
			
			// Extract bus ID
			parts := strings.Fields(trimmed)
			for i, part := range parts {
				if (part == "busid" || part == "BUSID") && i+1 < len(parts) {
					busid := parts[i+1]
					// Remove trailing characters
					busid = strings.TrimRight(busid, ":()")
					current.BusID = busid
					current.Bound = isDeviceBound(busid)
					break
				}
			}
		} else if current != nil && (strings.HasPrefix(line, " ") || strings.HasPrefix(line, "\t")) {
			// Device info line (indented)
			if current.Info != "" {
				current.Info += " "
			}
			current.Info += trimmed
		}
	}

	if current != nil {
		devices = append(devices, *current)
	}

	return devices
}

// updateDevices polls for device changes
func (s *Server) updateDevices() {
	devices, err := s.listDevices()
	if err != nil {
		s.logger.Printf("Failed to list devices: %v", err)
		return
	}

	s.mu.Lock()
	changed := !devicesEqual(s.devices, devices)
	s.devices = devices
	s.mu.Unlock()

	if changed {
		s.broadcastUpdate()
	}
}

// devicesEqual compares two device slices
func devicesEqual(a, b []Device) bool {
	if len(a) != len(b) {
		return false
	}

	for i := range a {
		if a[i].BusID != b[i].BusID || a[i].Bound != b[i].Bound {
			return false
		}
	}

	return true
}

// bindDevice binds a USB device
func (s *Server) bindDevice(busid string) error {
	if !validateBusID(busid) {
		return fmt.Errorf("invalid bus ID: %s", busid)
	}

	var cmd string
	var args []string

	if runtime.GOOS == "windows" {
		cmd = "usbipd"
		args = []string{"wsl", "attach", "--busid", busid}
	} else {
		cmd = "usbip"
		args = []string{"bind", "-b", busid}
	}

	_, err := execCommand(cmd, args...)
	if err != nil {
		return fmt.Errorf("failed to bind device: %w", err)
	}

	s.logger.Printf("Bound device: %s", busid)
	s.updateDevices()
	s.saveConfig()

	return nil
}

// unbindDevice unbinds a USB device
func (s *Server) unbindDevice(busid string) error {
	if !validateBusID(busid) {
		return fmt.Errorf("invalid bus ID: %s", busid)
	}

	var cmd string
	var args []string

	if runtime.GOOS == "windows" {
		cmd = "usbipd"
		args = []string{"wsl", "detach", "--busid", busid}
	} else {
		cmd = "usbip"
		args = []string{"unbind", "-b", busid}
	}

	_, err := execCommand(cmd, args...)
	if err != nil {
		return fmt.Errorf("failed to unbind device: %w", err)
	}

	s.logger.Printf("Unbound device: %s", busid)
	s.updateDevices()
	s.saveConfig()

	return nil
}

// SSE handlers
func (s *Server) broadcastUpdate() {
	s.clientMu.Lock()
	defer s.clientMu.Unlock()

	s.mu.RLock()
	devices := make([]Device, len(s.devices))
	copy(devices, s.devices)
	s.mu.RUnlock()

	for client := range s.clients {
		select {
		case client <- devices:
		default:
			// Client is slow, skip
		}
	}
}

func (s *Server) addClient(client chan []Device) {
	s.clientMu.Lock()
	s.clients[client] = struct{}{}
	s.clientMu.Unlock()
}

func (s *Server) removeClient(client chan []Device) {
	s.clientMu.Lock()
	delete(s.clients, client)
	close(client)
	s.clientMu.Unlock()
}

// HTTP handlers
func (s *Server) handleIndex(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}

	data, err := staticFiles.ReadFile("static/index.html")
	if err != nil {
		http.Error(w, "Internal Server Error", http.StatusInternalServerError)
		return
	}

	tmpl, err := template.New("index").Parse(string(data))
	if err != nil {
		http.Error(w, "Internal Server Error", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	tmpl.Execute(w, nil)
}

func (s *Server) handleDevices(w http.ResponseWriter, r *http.Request) {
	s.mu.RLock()
	devices := make([]Device, len(s.devices))
	copy(devices, s.devices)
	s.mu.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(devices)
}

func (s *Server) handleBind(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method Not Allowed", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		BusID string `json:"busid"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "Bad Request", http.StatusBadRequest)
		return
	}

	if err := s.bindDevice(req.BusID); err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
		return
	}

	s.mu.RLock()
	devices := make([]Device, len(s.devices))
	copy(devices, s.devices)
	s.mu.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":  "success",
		"devices": devices,
	})
}

func (s *Server) handleUnbind(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method Not Allowed", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		BusID string `json:"busid"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "Bad Request", http.StatusBadRequest)
		return
	}

	if err := s.unbindDevice(req.BusID); err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
		return
	}

	s.mu.RLock()
	devices := make([]Device, len(s.devices))
	copy(devices, s.devices)
	s.mu.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":  "success",
		"devices": devices,
	})
}

func (s *Server) handleEvents(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("Access-Control-Allow-Origin", "*")

	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "Streaming unsupported", http.StatusInternalServerError)
		return
	}

	client := make(chan []Device, 10)
	s.addClient(client)
	defer s.removeClient(client)

	// Send initial state
	s.mu.RLock()
	devices := make([]Device, len(s.devices))
	copy(devices, s.devices)
	s.mu.RUnlock()

	data, _ := json.Marshal(devices)
	fmt.Fprintf(w, "data: %s\n\n", data)
	flusher.Flush()

	// Stream updates
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-r.Context().Done():
			return
		case devices := <-client:
			data, _ := json.Marshal(devices)
			fmt.Fprintf(w, "data: %s\n\n", data)
			flusher.Flush()
		case <-ticker.C:
			fmt.Fprintf(w, ": heartbeat\n\n")
			flusher.Flush()
		}
	}
}

// startPolling starts the device polling loop
func (s *Server) startPolling(ctx context.Context) {
	ticker := time.NewTicker(s.config.PollInterval)
	defer ticker.Stop()

	// Initial update
	s.updateDevices()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			s.updateDevices()
		}
	}
}

// Run starts the server
func (s *Server) Run(ctx context.Context) error {
	mux := http.NewServeMux()
	mux.HandleFunc("/", s.handleIndex)
	mux.HandleFunc("/api/devices", s.handleDevices)
	mux.HandleFunc("/bind", s.handleBind)
	mux.HandleFunc("/unbind", s.handleUnbind)
	mux.HandleFunc("/events", s.handleEvents)

	// Serve static files
	mux.Handle("/static/", http.FileServer(http.FS(staticFiles)))

	addr := fmt.Sprintf("%s:%d", s.config.BindAddress, s.config.Port)
	server := &http.Server{
		Addr:         addr,
		Handler:      mux,
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	// Start polling in background
	go s.startPolling(ctx)

	// Get local IP for display
	localIP := getLocalIP()
	fmt.Printf("\n🚀 usbctl v%s server started successfully!\n", Version)
	fmt.Printf("📡 Server: %s\n", addr)
	fmt.Printf("🌐 Web interface URLs:\n")
	fmt.Printf("   http://localhost:%d\n", s.config.Port)
	if localIP != "localhost" {
		fmt.Printf("   http://%s:%d\n", localIP, s.config.Port)
	}
	fmt.Printf("📊 Status: Ready for connections\n")
	fmt.Printf("\n⚠️  Press Ctrl+C to stop the server gracefully\n\n")

	// Graceful shutdown
	go func() {
		<-ctx.Done()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		server.Shutdown(shutdownCtx)
	}()

	if err := server.ListenAndServe(); err != http.ErrServerClosed {
		return fmt.Errorf("server error: %w", err)
	}

	return nil
}

// getLocalIP returns the local IP address
func getLocalIP() string {
	conn, err := net.Dial("udp", "223.5.5.5:80")
	if err != nil {
		return "localhost"
	}
	defer conn.Close()

	localAddr := conn.LocalAddr().(*net.UDPAddr)
	return localAddr.IP.String()
}

func printUsage() {
	fmt.Printf("usbctl v%s - USB/IP Device Web Manager\n", Version)
	fmt.Printf("Author: %s\n\n", Author)
	fmt.Println("Usage: usbctl [OPTIONS]")
	fmt.Println("\nOptions:")
	fmt.Printf("  -p, --port PORT        Server port (default: %d)\n", DefaultPort)
	fmt.Printf("  -b, --bind ADDRESS     Bind address (default: %s)\n", DefaultBind)
	fmt.Println("  -i, --interval SEC     Polling interval (default: 3)")
	fmt.Println("  -c, --config PATH      Configuration file path")
	fmt.Println("  -v, --verbose          Enable verbose logging")
	fmt.Println("      --version          Show version")
	fmt.Println("      --help             Show this help")
	fmt.Println("\nExamples:")
	fmt.Println("  usbctl                 # Start web server")
	fmt.Println("  usbctl -p 8080         # Start on port 8080")
	fmt.Println("  usbctl -v              # Start with verbose logging")
}

func main() {
	// Command-line flags
	var (
		port       = flag.Int("p", 0, "Server port")
		portLong   = flag.Int("port", 0, "Server port")
		bind       = flag.String("b", "", "Bind address")
		bindLong   = flag.String("bind", "", "Bind address")
		interval   = flag.Int("i", 0, "Polling interval in seconds")
		intervalL  = flag.Int("interval", 0, "Polling interval in seconds")
		configPath = flag.String("c", "", "Configuration file path")
		configLong = flag.String("config", "", "Configuration file path")
		verbose    = flag.Bool("v", false, "Enable verbose logging")
		verboseLong= flag.Bool("verbose", false, "Enable verbose logging")
		version    = flag.Bool("version", false, "Show version")
		help       = flag.Bool("help", false, "Show help")
	)

	flag.Parse()

	if *help {
		printUsage()
		return
	}

	if *version {
		fmt.Printf("usbctl version %s\n", Version)
		return
	}

	// Merge short and long flags
	if *portLong != 0 {
		port = portLong
	}
	if *bindLong != "" {
		bind = bindLong
	}
	if *intervalL != 0 {
		interval = intervalL
	}
	if *configLong != "" {
		configPath = configLong
	}
	if *verboseLong {
		verbose = verboseLong
	}

	// Determine config path
	cfgPath := *configPath
	if cfgPath == "" {
		if runtime.GOOS == "windows" {
			cfgPath = filepath.Join(os.Getenv("LOCALAPPDATA"), "usbctl", "config")
		} else {
			cfgPath = "/etc/usbctl/config"
		}
	}

	// Load configuration
	cfg, err := loadConfig(cfgPath)
	if err != nil {
		log.Fatalf("Failed to load config: %v", err)
	}

	cfg.ConfigPath = cfgPath

	// Override with command-line flags
	if *port != 0 {
		cfg.Port = *port
	}
	if *bind != "" {
		cfg.BindAddress = *bind
	}
	if *interval != 0 {
		cfg.PollInterval = time.Duration(*interval) * time.Second
	}
	if *verbose {
		cfg.VerboseLog = true
	}

	// Create server
	server := NewServer(cfg)

	// Setup signal handling for graceful shutdown
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	go func() {
		<-sigChan
		fmt.Println("\n\n⏹️  Shutting down gracefully...")
		cancel()
	}()

	// Run server
	if err := server.Run(ctx); err != nil {
		log.Fatalf("Server error: %v", err)
	}

	fmt.Println("✅ Server stopped successfully")
}