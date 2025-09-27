/*
 * usbctl - USB/IP Device Web Manager
 * A lightweight single-file web interface for USB/IP device management
 *
 * Features:
 * - Single executable file (~300KB static build)
 * - Embedded HTML/CSS/JS resources
 * - Configuration persistence
 * - Real-time Server-Sent Events updates
 * - AJAX-based device operations
 * - ARM Linux optimized
 *
 * Build: gcc -static -O2 -o usbctl usbctl.c -lpthread
 * Usage: ./usbctl [options]
 */

// Enable POSIX/GNU extensions for readlink and others before any headers
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>

#define VERSION "1.0.0"
#define AUTHOR "github.com/suifei"
#define DEFAULT_PORT 11980
#define DEFAULT_BIND "0.0.0.0"
#define MAX_CLIENTS 10
#define BUFFER_SIZE 8192
#define CONFIG_PATH_SIZE 512
#define MAX_DEVICES 32
#define LOG_BUFFER_SIZE 1024

// Configuration structure
typedef struct
{
    int port;
    char bind_address[64];
    int poll_interval;
    char config_path[CONFIG_PATH_SIZE];
    int verbose_logging;
    char log_file[256];
} config_t;
// HTTP响应函数声明
void send_http_response(int client_socket, int status_code, const char *status_text, const char *content_type, const char *body);

// USB device structure
typedef struct
{
    char busid[16];
    char info[256];
    int bound;
} usb_device_t;

// SSE client structure
typedef struct
{
    int socket;
    int is_sse;
    struct sockaddr_in addr;
    time_t last_heartbeat;
} client_t;

// Global variables
static config_t g_config = {DEFAULT_PORT, DEFAULT_BIND, 3, "", 1, "/var/log/usbctl.log"};
static usb_device_t g_devices[MAX_DEVICES];
static int g_device_count = 0;
static client_t g_clients[MAX_CLIENTS];
static int g_client_count = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_running = 1;
static volatile int g_server_started = 0;
static FILE *g_log_file = NULL;
static int g_usbip_error_shown = 0;

// ============================================================================
// EMBEDDED WEB RESOURCES (Minimized and optimized)
// ============================================================================

// Embedded Logo SVG
const char *LOGO_SVG =
    "<svg width=\"64\" height=\"64\" viewBox=\"0 0 64 64\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect x=\"12\" y=\"24\" width=\"24\" height=\"16\" rx=\"2\" fill=\"#007BFF\" stroke=\"#0056b3\" stroke-width=\"2\"/>"
    "<rect x=\"36\" y=\"28\" width=\"4\" height=\"2\" fill=\"#fff\"/>"
    "<rect x=\"36\" y=\"32\" width=\"4\" height=\"2\" fill=\"#fff\"/>"
    "<path d=\"M40 32 L52 32\" stroke=\"#007BFF\" stroke-width=\"3\" stroke-linecap=\"round\"/>"
    "<circle cx=\"52\" cy=\"32\" r=\"6\" fill=\"#28a745\" stroke=\"#1e7e34\" stroke-width=\"2\"/>"
    "<circle cx=\"52\" cy=\"32\" r=\"2\" fill=\"#fff\"/>"
    "</svg>";

// Embedded favicon.ico (base64 encoded 16x16 USB connector icon - blue and green design like SVG logo)
static const char *EMBEDDED_FAVICON =
    "AAABAAEAEBAAAAEAIABoBAAAFgAAACgAAAAQAAAAIAAAAAEAIAAAAAAAAAQAABILAAASCwAAAAAAAAAAAAD"
    "///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A"
    "////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP"
    "///wD///8A////AP///wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wD/"
    "//8A////AP///wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wD///8A///"
    "/AP///wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wD///8A////AP//"
    "/wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wD///8A////AP///wAH"
    "tP8AB7T/AAe0/wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wAHtP8A"
    "B7T/AAe0/wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wD///8A////"
    "AP///wD///8A////AP///wAoqAAAs+0AAP///wD///8A////AP///wD///8A////AP///wD///8A////AP//"
    "/wD///8A////ACoqKgAoqAAAKKgAAP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD/"
    "//8A////ACoqKgAoqAAAKKgAAP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A"
    "////ACoqKgAoqAAAKKgAAP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A///"
    "/ACoqKgAoqAAAKKgAAP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////A"
    "CoqKgAoqAAAKKgAAP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////ACo"
    "qKgAoqAAA////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///"
    "wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8AAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

// Embedded CSS (Ultra-compact responsive design with modern aesthetics)
static const char *EMBEDDED_CSS =
    "*{margin:0;padding:0;box-sizing:border-box}"
    "body{font:14px/1.4 system-ui,-apple-system,sans-serif;background:#f8f9fa;color:#333;padding:10px;padding-bottom:220px}"
    "header{text-align:center;margin-bottom:20px;padding:10px;background:linear-gradient(135deg,#007bff,#6610f2);color:#fff;border-radius:8px}"
    ".logo{width:32px;height:32px;margin:0 8px;vertical-align:middle}"
    "h1{font-size:24px;margin:5px 0}"
    ".controls{text-align:center;margin-bottom:15px}"
    ".lang-btn{background:#28a745;color:#fff;border:none;padding:4px 8px;border-radius:4px;margin:0 2px;cursor:pointer;font-size:11px}"
    ".lang-btn.active{background:#155724}"
    ".main-content{margin-bottom:20px}"
    "table{width:100%;border-collapse:collapse;background:#fff;border-radius:8px;overflow:hidden;box-shadow:0 2px 8px rgba(0,0,0,0.1)}"
    "th,td{padding:8px 12px;text-align:left;border-bottom:1px solid #dee2e6;font-size:13px}"
    "th{background:#007bff;color:#fff;font-weight:600;position:sticky;top:0}"
    "tr:hover{background:#f8f9fa}"
    ".status{font-weight:600;padding:2px 6px;border-radius:3px;font-size:11px}"
    ".bound{background:#d4edda;color:#155724}"
    ".unbound{background:#f8d7da;color:#721c24}"
    "button{padding:4px 8px;border:none;border-radius:3px;cursor:pointer;font-size:11px;transition:all 0.2s}"
    ".btn-bind{background:#28a745;color:#fff}"
    ".btn-unbind{background:#dc3545;color:#fff}"
    ".btn-bind:hover{background:#218838}"
    ".btn-unbind:hover{background:#c82333}"
    "#status{position:fixed;top:10px;right:10px;padding:4px 8px;background:#17a2b8;color:#fff;border-radius:4px;font-size:11px;z-index:1000}"
    ".footer{position:fixed;bottom:0;left:0;right:0;text-align:center;font-size:11px;color:#6c757d;background:rgba(248,249,250,0.98);padding:8px 15px;border-top:1px solid #dee2e6;backdrop-filter:blur(10px);z-index:999}"
    ".footer a{color:#007bff;text-decoration:none}"
    ".log-container{position:fixed;bottom:40px;left:0;right:0;height:150px;background:#fff;border-top:2px solid #007bff;box-shadow:0 -2px 10px rgba(0,0,0,0.1);z-index:998}"
    ".log-header{display:flex;justify-content:space-between;align-items:center;padding:8px 15px;background:#f8f9fa;border-bottom:1px solid #dee2e6}"
    ".log-title{font-weight:600;color:#333;font-size:13px}"
    ".clear-log-btn{background:#6c757d;color:#fff;padding:3px 8px;border:none;border-radius:3px;cursor:pointer;font-size:11px}"
    ".clear-log-btn:hover{background:#5a6268}"
    ".log-content{height:calc(150px - 40px);overflow-y:auto;padding:8px;font-family:monospace;font-size:12px;line-height:1.3}"
    ".log-entry{margin-bottom:4px;padding:2px 0;border-left:3px solid transparent;padding-left:8px}"
    ".log-success{color:#155724;border-left-color:#28a745;background:#d4edda}"
    ".log-error{color:#721c24;border-left-color:#dc3545;background:#f8d7da}"
    ".log-info{color:#0c5460;border-left-color:#17a2b8;background:#d1ecf1}"
    ".log-timestamp{color:#6c757d;font-size:10px}"
    "@media(max-width:768px){"
    "body{padding:5px;padding-bottom:200px}"
    "h1{font-size:20px}"
    "th,td{padding:6px 8px;font-size:12px}"
    "button{padding:3px 6px;font-size:10px}"
    ".logo{width:24px;height:24px}"
    ".controls{margin-bottom:10px}"
    ".footer{padding:6px 10px;font-size:10px}"
    ".log-container{height:130px;bottom:35px}"
    ".log-content{height:calc(130px - 35px);font-size:11px}"
    ".log-header{padding:6px 12px}"
    ".log-title{font-size:12px}"
    "}";

// Embedded JavaScript (Ultra-optimized with smart i18n)
const char *EMBEDDED_JS =
    "let eventSource,devices=[],lang='en',logEntries=[];"
    "const i18n={'en':{'title':'USB/IP Manager','device':'Device Info','busid':'Bus ID','status':'Status','action':'Action','bound':'Bound','unbound':'Unbound','bind':'Bind','unbind':'Unbind','connected':'Connected','disconnected':'Disconnected','error':'Error','author':'Author','log_title':'Operation Log','clear':'Clear','bind_success':'Device {busid} bound successfully','unbind_success':'Device {busid} unbound successfully','bind_error':'Error binding device {busid}: {error}','unbind_error':'Error unbinding device {busid}: {error}'},'zh':{'title':'USB/IP 管理器','device':'设备信息','busid':'总线ID','status':'状态','action':'操作','bound':'已绑定','unbound':'未绑定','bind':'绑定','unbind':'解绑','connected':'已连接','disconnected':'已断开','error':'错误','author':'作者','log_title':'操作日志','clear':'清除','bind_success':'设备 {busid} 绑定成功','unbind_success':'设备 {busid} 解绑成功','bind_error':'绑定设备 {busid} 失败: {error}','unbind_error':'解绑设备 {busid} 失败: {error}'}};"
    "function detectLang(){"
    "try{"
    "const stored=localStorage.getItem('usbctl_lang');"
    "if(stored&&i18n[stored])return stored;"
    "const nav=navigator.language||navigator.userLanguage||navigator.browserLanguage||'en';"
    "const langCode=nav.toLowerCase();"
    "if(langCode.startsWith('zh')||langCode.includes('chinese')||langCode.includes('cn'))return 'zh';"
    "return 'en';"
    "}catch(e){return 'en';}"
    "}"
    "function t(k,vars){let text=i18n[lang][k]||k;if(vars){Object.keys(vars).forEach(key=>{text=text.replace(`{${key}}`,vars[key]);})}return text;}"
    "function setLang(l){lang=l;localStorage.setItem('usbctl_lang',l);updateUI();}"
    "function updateUI(){"
    "document.title=`usbctl - ${t('title')}`;"
    "document.querySelector('h1').textContent=t('title');"
    "document.documentElement.lang=lang==='zh'?'zh-CN':'en';"
    "const ths=document.querySelectorAll('th');"
    "if(ths.length>=4){ths[0].textContent=t('device');ths[1].textContent=t('busid');ths[2].textContent=t('status');ths[3].textContent=t('action');}"
    "document.querySelectorAll('.lang-btn').forEach(b=>b.classList.toggle('active',b.dataset.lang===lang));"
    "const logTitle=document.querySelector('.log-title');"
    "if(logTitle)logTitle.textContent=t('log_title');"
    "const clearBtn=document.querySelector('.clear-log-btn');"
    "if(clearBtn)clearBtn.textContent=t('clear');"
    "render();renderLog();"
    "}"
    "function connectSSE(){"
    "if(eventSource)eventSource.close();"
    "eventSource=new EventSource('/events');"
    "eventSource.onopen=()=>document.getElementById('status').textContent=t('connected');"
    "eventSource.onmessage=e=>{"
    "try{devices=JSON.parse(e.data);render();}catch(err){console.error(err);}"
    "};"
    "eventSource.onerror=()=>{"
    "document.getElementById('status').textContent=t('disconnected');"
    "setTimeout(connectSSE,3000);"
    "};"
    "}"
    "function render(){"
    "const tbody=document.querySelector('tbody');"
    "tbody.innerHTML=devices.map(d=>"
    "`<tr><td>${d.info}</td><td><code>${d.busid}</code></td>`+"
    "`<td><span class=\"status ${d.bound?'bound':'unbound'}\">${t(d.bound?'bound':'unbound')}</span></td>`+"
    "`<td><button class=\"${d.bound?'btn-unbind':'btn-bind'}\" onclick=\"toggle('${d.busid}')\">`+"
    "`${t(d.bound?'unbind':'bind')}</button></td></tr>`).join('');"
    "}"
    "function addLog(type,message){"
    "const timestamp=new Date().toLocaleTimeString();"
    "const entry={type,message,timestamp};"
    "logEntries.unshift(entry);"
    "if(logEntries.length>100)logEntries.pop();"
    "renderLog();"
    "}"
    "function renderLog(){"
    "const logContent=document.getElementById('logContent');"
    "if(!logContent)return;"
    "logContent.innerHTML=logEntries.map(entry=>"
    "`<div class=\"log-entry log-${entry.type}\">`+"
    "`<span class=\"log-timestamp\">[${entry.timestamp}]</span> ${entry.message}`+"
    "`</div>`).join('');"
    "logContent.scrollTop=0;"
    "}"
    "function clearLog(){"
    "logEntries=[];"
    "renderLog();"
    "}"
    "function toggle(busid){"
    "const device=devices.find(d=>d.busid===busid);"
    "if(!device)return;"
    "const button=event.target;"
    "const action=device.bound?'unbind':'bind';"
    "button.disabled=true;"
    "fetch(`/${action}`,{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({busid})})"
    ".then(response=>{"
    "if(response.ok){"
    "addLog('success',t(`${action}_success`,{busid}));"
    "loadDevices();"
    "}else{"
    "return response.text().then(text=>{"
    "let errorMsg='Unknown error';"
    "try{"
    "const data=JSON.parse(text);"
    "if(data.error){"
    "errorMsg=data.error.trim();"
    "const errorMatch=errorMsg.match(/error[:\\s]*(.+?)(?:\\n|$)/i);"
    "if(errorMatch)errorMsg=errorMatch[1];"
    "}"
    "}catch(e){"
    "const errorMatch=text.match(/error[:\\s]*(.+?)(?:\\n|$)/i);"
    "if(errorMatch)errorMsg=errorMatch[1];"
    "}"
    "addLog('error',t(`${action}_error`,{busid,error:errorMsg}));"
    "throw new Error(errorMsg);"
    "});"
    "}"
    "})"
    ".catch(err=>{"
    "console.error(err);"
    "})"
    ".finally(()=>button.disabled=false);"
    "}"
    "function loadDevices(){"
    "fetch('/api/devices').then(r=>r.json()).then(data=>{devices=data;render()}).catch(console.error);"
    "}"
    "window.onload=()=>{"
    "lang=detectLang();"
    "updateUI();connectSSE();loadDevices();"
    "};";

// Embedded HTML (Optimized responsive template)
const char *EMBEDDED_HTML =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,user-scalable=no\">"
    "<title>usbctl - USB/IP Manager</title><style>%s</style></head><body>"
    "<header><div class=\"logo\">%s</div><h1>USB/IP Manager</h1>"
    "</header>"
    "<div class=\"controls\">"
    "<button class=\"lang-btn active\" data-lang=\"en\" onclick=\"setLang('en')\">English</button>"
    "<button class=\"lang-btn\" data-lang=\"zh\" onclick=\"setLang('zh')\">中文</button>"
    "</div>"
    "<div id=\"status\">Connecting...</div>"
    "<div class=\"main-content\">"
    "<table><thead><tr><th>Device Info</th><th>Bus ID</th><th>Status</th><th>Action</th></tr></thead>"
    "<tbody></tbody></table>"
    "</div>"
    "<div class=\"log-container\">"
    "<div class=\"log-header\">"
    "<span class=\"log-title\">Operation Log</span>"
    "<button class=\"clear-log-btn\" onclick=\"clearLog()\">Clear</button>"
    "</div>"
    "<div class=\"log-content\" id=\"logContent\"></div>"
    "</div>"
    "<div class=\"footer\">Powered by <a href=\"https://github.com/suifei/usbctl\" target=\"_blank\">usbctl v" VERSION "</a> | "
    "<a href=\"https://github.com/suifei\" target=\"_blank\">github.com/suifei</a></div>"
    "<script>%s</script></body></html>";

// ============================================================================
// LOGGING AND SYSTEM COMPATIBILITY
// ============================================================================

// Initialize logging system
int init_logging()
{
    if (!g_config.verbose_logging)
    {
        g_log_file = fopen("/dev/null", "w");
        return g_log_file != NULL;
    }

    // Try to open log file
    g_log_file = fopen(g_config.log_file, "a");
    if (!g_log_file)
    {
        // Fallback to stderr
        g_log_file = stderr;
        fprintf(stderr, "[WARN] Cannot open log file %s, using stderr\n", g_config.log_file);
    }
    return 1;
}

// Log message with timestamp
void log_message(const char *level, const char *format, ...)
{
    if (!g_log_file)
        return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(g_log_file, "[%s] %s: ", timestamp, level);

    va_list args;
    va_start(args, format);
    vfprintf(g_log_file, format, args);
    va_end(args);

    fprintf(g_log_file, "\n");
    fflush(g_log_file);
}

// Check system compatibility
int check_system_compatibility()
{
    // Cross-platform compatibility check
    const char *platform =
#ifdef __linux__
        "Linux";
#elif __APPLE__
        "macOS";
#elif _WIN32
        "Windows";
#else
        "Unix-like";
#endif

    log_message("INFO", "Running on %s platform", platform);

    // Check for usbip command availability with different search patterns per platform
#ifdef __linux__
    if (access("/usr/bin/usbip", X_OK) != 0 &&
        access("/usr/sbin/usbip", X_OK) != 0 &&
        access("/bin/usbip", X_OK) != 0 &&
        access("/sbin/usbip", X_OK) != 0)
    {
        log_message("WARN", "usbip command not found. Install: sudo apt install linux-tools-generic");
    }

    // Check if usbip-host module is available
    if (access("/sys/bus/usb/drivers/usbip-host", F_OK) != 0)
    {
        log_message("WARN", "usbip-host driver not found. Run: sudo modprobe usbip-host");
    }

    // Check root privileges on Linux
    if (geteuid() != 0)
    {
        log_message("WARN", "Not running as root. Some USB operations may fail");
    }

#elif __APPLE__
    // macOS: Check for third-party usbip implementations
    if (access("/usr/local/bin/usbip", X_OK) != 0 &&
        access("/opt/homebrew/bin/usbip", X_OK) != 0 &&
        access("/usr/bin/usbip", X_OK) != 0)
    {
        log_message("WARN", "usbip command not found. Limited functionality available");
        log_message("INFO", "Third-party usbip tools may be available for macOS");
    }

    if (geteuid() != 0)
    {
        log_message("INFO", "Running as regular user on macOS (development mode)");
    }

#elif _WIN32
    log_message("INFO", "Windows support requires usbipd-win or usbip-win");

#else
    log_message("WARN", "Untested platform. Web interface functionality available");
#endif

    return 1; // Always succeed - allow running in limited mode
}

// Get local IP address for display
char *get_local_ip()
{
    static char ip_str[INET_ADDRSTRLEN] = "localhost";
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return ip_str;

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr("223.5.5.5"); // Use Aliyun DNS for route discovery
    dest.sin_port = htons(80);

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) == 0)
    {
        struct sockaddr_in local;
        socklen_t local_len = sizeof(local);
        if (getsockname(sock, (struct sockaddr *)&local, &local_len) == 0)
        {
            inet_ntop(AF_INET, &local.sin_addr, ip_str, INET_ADDRSTRLEN);
        }
    }
    close(sock);
    return ip_str;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Get home directory path
const char *get_home_dir()
{
    const char *home = getenv("HOME");
    return home ? home : "/tmp";
}

// Initialize default configuration
void init_config()
{
    snprintf(g_config.config_path, sizeof(g_config.config_path),
             "%s/.config/usbctl/config", get_home_dir());
}

// Create directory recursively
int mkdirs(const char *path)
{
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
    {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

// Load configuration from file
int load_config()
{
    FILE *fp = fopen(g_config.config_path, "r");
    if (!fp)
        return 0;

    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "port=", 5) == 0)
        {
            g_config.port = atoi(line + 5);
        }
        else if (strncmp(line, "bind=", 5) == 0)
        {
            sscanf(line + 5, "%63s", g_config.bind_address);
        }
        else if (strncmp(line, "poll_interval=", 14) == 0)
        {
            g_config.poll_interval = atoi(line + 14);
        }
        else if (strncmp(line, "verbose_logging=", 16) == 0)
        {
            g_config.verbose_logging = atoi(line + 16);
        }
        else if (strncmp(line, "log_file=", 9) == 0)
        {
            sscanf(line + 9, "%255s", g_config.log_file);
        }
    }
    fclose(fp);
    return 1;
}

// Save configuration to file
int save_config()
{
    // Create config directory if it doesn't exist
    char dir_path[512];
    strncpy(dir_path, g_config.config_path, sizeof(dir_path));
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash)
    {
        *last_slash = '\0';
        mkdirs(dir_path);
    }

    FILE *fp = fopen(g_config.config_path, "w");
    if (!fp)
        return 0;

    fprintf(fp, "port=%d\n", g_config.port);
    fprintf(fp, "bind=%s\n", g_config.bind_address);
    fprintf(fp, "poll_interval=%d\n", g_config.poll_interval);
    fclose(fp);
    return 1;
}

// ============================================================================
// USB/IP BACKEND FUNCTIONS
// ============================================================================

// Execute command and capture output
int exec_command(const char *cmd, char *output, size_t output_size)
{
    if (!cmd || !output || output_size == 0)
    {
        log_message("ERROR", "Invalid arguments for exec_command");
        return -1;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp)
    {
        log_message("ERROR", "Failed to execute command: %s", cmd);
        return -1;
    }

    size_t total = 0;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp) && total < output_size - 1)
    {
        size_t len = strlen(buffer);
        if (total + len < output_size - 1)
        {
            strcpy(output + total, buffer);
            total += len;
        }
    }
    output[total] = '\0';

    int result = pclose(fp);
    // Only log command output during startup or if verbose logging enabled
    if (!g_server_started || g_config.verbose_logging)
    {
        log_message("DEBUG", "Command output: %s", output);
    }
    return WEXITSTATUS(result);
}

// Check if device is bound
int is_device_bound(const char *busid)
{
    char path[256];
    snprintf(path, sizeof(path), "/sys/bus/usb/drivers/usbip-host/%s", busid);
    return access(path, F_OK) == 0;
}

// List USB/IP devices with enhanced compatibility
int list_usbip_devices()
{
    char output[4096];

    // Try different usbip command variations for compatibility
    const char *usbip_commands[] = {
        "usbip list -l",           // Standard command
        "/usr/bin/usbip list -l",  // Explicit path
        "/usr/sbin/usbip list -l", // Alternative path
        "usbip list --local",      // Alternative syntax
        NULL};

    int cmd_success = 0;
    for (int i = 0; usbip_commands[i] != NULL; i++)
    {
        if (exec_command(usbip_commands[i], output, sizeof(output)) == 0)
        {
            cmd_success = 1;
            // Only log command success during startup or if verbose logging enabled
            if (!g_server_started || g_config.verbose_logging)
            {
                log_message("DEBUG", "Successfully executed: %s", usbip_commands[i]);
            }
            break;
        }
    }

    if (!cmd_success)
    {
        if (!g_usbip_error_shown)
        {
            log_message("ERROR", "Failed to execute usbip command. Ensure usbip tools are installed");
            g_usbip_error_shown = 1;
        }
        return 0;
    }

    g_device_count = 0;
    char *line = strtok(output, "\n");
    usb_device_t *current = NULL;

    while (line && g_device_count < MAX_DEVICES)
    {
        // Trim whitespace
        while (*line == ' ' || *line == '\t')
            line++;

        // Handle different output formats
        if (strncmp(line, "- busid", 7) == 0 || strstr(line, "busid"))
        {
            // New device found
            current = &g_devices[g_device_count++];
            memset(current, 0, sizeof(usb_device_t));

            // Extract busid - handle various formats
            char *busid_start = NULL;
            if ((busid_start = strstr(line, "busid ")) != NULL)
            {
                busid_start += 6;
            }
            else if ((busid_start = strchr(line, ' ')) != NULL)
            {
                busid_start++;
            }

            if (busid_start)
            {
                while (*busid_start == ' ')
                    busid_start++;
                sscanf(busid_start, "%15s", current->busid);
                current->bound = is_device_bound(current->busid);
                // Only log device discovery during startup or if verbose logging enabled
                if (!g_server_started || g_config.verbose_logging)
                {
                    log_message("DEBUG", "Found device: %s (bound: %d)", current->busid, current->bound);
                }
            }
        }
        else if (current && (strchr(line, ':') || strstr(line, "ID ")))
        {
            // Device info line - handle various description formats
            if (strlen(current->info) > 0)
            {
                strncat(current->info, " ", sizeof(current->info) - strlen(current->info) - 1);
            }

            // Clean up common prefixes and unnecessary whitespace
            char *clean_line = line;
            while (*clean_line == ' ' || *clean_line == '\t')
                clean_line++;
            if (strncmp(clean_line, "ID ", 3) == 0)
                clean_line += 3;

            strncat(current->info, clean_line, sizeof(current->info) - strlen(current->info) - 1);
        }
        line = strtok(NULL, "\n");
    }

    return g_device_count;
}

// Bind USB device
int bind_device(const char *busid)
{
    if (!busid || strlen(busid) == 0)
    {
        log_message("ERROR", "Invalid busid: %s", busid ? busid : "(null)");
        return 0;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "usbip bind -b %s", busid);
    log_message("INFO", "Attempting to bind device: %s", busid);

    int result = exec_command(cmd, NULL, 0) == 0;
    if (result)
    {
        log_message("INFO", "Successfully bound device: %s", busid);
    }
    else
    {
        log_message("ERROR", "Failed to bind device: %s", busid);
    }

    return result;
}

// 命令行专用解绑函数
int unbind_device(const char *busid)
{
    if (!busid || strlen(busid) == 0)
    {
        log_message("ERROR", "Invalid busid: %s", busid ? busid : "(null)");
        return 0;
    }
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "usbip unbind -b %s", busid);
    log_message("INFO", "Attempting to unbind device: %s", busid);
    char output[1024];
    int result = exec_command(cmd, output, sizeof(output)) == 0;
    log_message("DEBUG", "Command output: %s", output);
    return result;
}

// web专用解绑函数
int unbind_device_web(int client_socket, const char *busid)
{
    if (!busid || strlen(busid) == 0)
    {
        log_message("ERROR", "Invalid busid: %s", busid ? busid : "(null)");
        send_http_response(client_socket, 400, "Bad Request", "application/json", "{\"status\":\"failed\",\"error\":\"Invalid busid\"}");
        return 0;
    }
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "usbip unbind -b %s", busid);
    log_message("INFO", "Attempting to unbind device: %s", busid);
    char output[1024];
    int result = exec_command(cmd, output, sizeof(output)) == 0;
    if (result)
    {
        log_message("INFO", "Successfully unbound device: %s", busid);
        send_http_response(client_socket, 200, "OK", "application/json", output);
    }
    else
    {
        log_message("ERROR", "Failed to unbind device: %s", busid);
        send_http_response(client_socket, 500, "Internal Server Error", "application/json", output);
    }
    return result;
}

// ============================================================================
// HTTP SERVER FUNCTIONS
// ============================================================================

// Generate full HTML page
void generate_html_page(char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size, EMBEDDED_HTML, EMBEDDED_CSS, LOGO_SVG, EMBEDDED_JS);
}

// Send HTTP response
void send_http_response(int client_socket, int status_code, const char *status_text,
                        const char *content_type, const char *body)
{
    char header[512];
    int body_len = body ? strlen(body) : 0;

    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n",
             status_code, status_text, content_type, body_len);

    send(client_socket, header, strlen(header), 0);
    if (body)
    {
        send(client_socket, body, body_len, 0);
    }
}

// Generate JSON for devices
void generate_devices_json(char *buffer, size_t buffer_size)
{
    if (buffer_size == 0) return;
    buffer[0] = '\0';
    strncat(buffer, "[", buffer_size - 1);
    for (int i = 0; i < g_device_count; i++)
    {
        // Sanitize / truncate info to avoid overly large SSE frames
        const char *info_src = g_devices[i].info;
        char info_sanitized[256];
        size_t k = 0;
        for (size_t j = 0; info_src[j] && k < sizeof(info_sanitized) - 1; j++)
        {
            unsigned char c = (unsigned char)info_src[j];
            // Escape quotes and backslashes minimally
            if (c == '"' || c == '\\') {
                if (k + 2 >= sizeof(info_sanitized) - 1) break;
                info_sanitized[k++] = '\\';
                info_sanitized[k++] = c;
            } else if ((c >= 32 && c < 127)) {
                info_sanitized[k++] = c;
            } else {
                // skip non printable
            }
        }
        info_sanitized[k] = '\0';

        char device_json[384];
        int written = snprintf(device_json, sizeof(device_json),
                               "%s{\"busid\":\"%s\",\"info\":\"%s\",\"bound\":%s}",
                               i > 0 ? "," : "",
                               g_devices[i].busid,
                               info_sanitized,
                               g_devices[i].bound ? "true" : "false");
        if (written < 0) continue;
        size_t remaining = buffer_size - strlen(buffer) - 1;
        if ((size_t)written > remaining) {
            // No more space; close array and stop
            strncat(buffer, "]", buffer_size - strlen(buffer) - 1);
            return;
        }
        strncat(buffer, device_json, remaining);
    }
    strncat(buffer, "]", buffer_size - strlen(buffer) - 1);
}

// Send SSE message
ssize_t send_sse_message(int client_socket, const char *data)
{
    // Frame limited to 4KB to avoid truncation warnings and large packets
    char response[4096];
    const size_t prefix_len = 6; // "data: "
    const size_t suffix_len = 2; // "\n\n"
    size_t max_payload = sizeof(response) - prefix_len - suffix_len - 1;
    size_t data_len = strlen(data);
    if (data_len > max_payload) data_len = max_payload;
    memcpy(response, "data: ", prefix_len);
    memcpy(response + prefix_len, data, data_len);
    response[prefix_len + data_len] = '\n';
    response[prefix_len + data_len + 1] = '\n';
    response[prefix_len + data_len + 2] = '\0';
    return send(client_socket, response, prefix_len + data_len + suffix_len, MSG_NOSIGNAL);
}

// Send SSE headers
void send_sse_headers(int client_socket)
{
    const char *headers = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    send(client_socket, headers, strlen(headers), MSG_NOSIGNAL);
}

// Base64 decode function
static int base64_decode(const char *input, unsigned char *output, int max_len)
{
    const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int len = 0, i = 0, j = 0;
    unsigned char temp[4];

    while (input[i] && len < max_len - 3)
    {
        // Skip whitespace and padding
        if (input[i] == '=' || input[i] == '\n' || input[i] == '\r' || input[i] == ' ')
        {
            i++;
            continue;
        }

        // Find character in table
        const char *p = strchr(table, input[i]);
        if (!p)
        {
            i++;
            continue;
        }
        temp[j++] = p - table;

        if (j == 4)
        {
            output[len++] = (temp[0] << 2) | (temp[1] >> 4);
            if (len < max_len)
                output[len++] = (temp[1] << 4) | (temp[2] >> 2);
            if (len < max_len)
                output[len++] = (temp[2] << 6) | temp[3];
            j = 0;
        }
        i++;
    }
    return len;
}

// ============================================================================
// CLIENT MANAGEMENT
// ============================================================================

// Add SSE client
void add_sse_client(int socket, struct sockaddr_in addr)
{
    pthread_mutex_lock(&g_mutex);
    if (g_client_count < MAX_CLIENTS)
    {
        g_clients[g_client_count].socket = socket;
        g_clients[g_client_count].is_sse = 1;
        g_clients[g_client_count].addr = addr;
        g_clients[g_client_count].last_heartbeat = time(NULL);
        g_client_count++;
    }
    pthread_mutex_unlock(&g_mutex);
}

// Remove client
void remove_client(int socket)
{
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_client_count; i++)
    {
        if (g_clients[i].socket == socket)
        {
            // Move last client to this position
            g_clients[i] = g_clients[g_client_count - 1];
            g_client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex);
}

// Broadcast to all SSE clients
void broadcast_devices_update()
{
    char json[4096];
    generate_devices_json(json, sizeof(json));

    pthread_mutex_lock(&g_mutex);
    
    // 安全检查：确保客户端列表有效
    if (g_client_count <= 0) {
        pthread_mutex_unlock(&g_mutex);
        return;
    }
    
    for (int i = g_client_count - 1; i >= 0; i--)
    {
        if (g_clients[i].is_sse)
        {
            // Send as SSE message
            ssize_t result = send_sse_message(g_clients[i].socket, json);
            if (result <= 0)
            {
                close(g_clients[i].socket);
                // Remove client by moving last client to current position
                if (i < g_client_count - 1) {
                    g_clients[i] = g_clients[g_client_count - 1];
                }
                g_client_count--;
            }
            else
            {
                g_clients[i].last_heartbeat = time(NULL);
            }
        }
    }
    pthread_mutex_unlock(&g_mutex);
}

// ============================================================================
// SERVER THREADS
// ============================================================================

// Device polling thread
void *device_poll_thread(void *arg)
{
    (void)arg; // Unused parameter
    usb_device_t prev_devices[MAX_DEVICES];
    int prev_count = 0;

    while (g_running)
    {
        // Save previous state
        memcpy(prev_devices, g_devices, sizeof(prev_devices));
        prev_count = g_device_count;

        // Update device list
        list_usbip_devices();

        // Check for changes
        int changed = 0;
        if (prev_count != g_device_count)
        {
            changed = 1;
        }
        else
        {
            for (int i = 0; i < g_device_count; i++)
            {
                if (strcmp(prev_devices[i].busid, g_devices[i].busid) != 0 ||
                    prev_devices[i].bound != g_devices[i].bound)
                {
                    changed = 1;
                    break;
                }
            }
        }

        if (changed)
        {
            broadcast_devices_update();
        }

        sleep(g_config.poll_interval);
    }
    return NULL;
}

// Handle client request
void *handle_client(void *arg)
{
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    ssize_t received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

    if (received <= 0)
    {
        close(client_socket);
        return NULL;
    }

    buffer[received] = '\0';

    // Parse HTTP request
    char method[16], path[256], version[16];
    sscanf(buffer, "%15s %255s %15s", method, path, version);

    // Handle SSE events endpoint
    if (strcmp(path, "/events") == 0 && strcmp(method, "GET") == 0)
    {
        // Send SSE headers
        send_sse_headers(client_socket);
        
        // Add client to SSE clients list
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        getpeername(client_socket, (struct sockaddr *)&addr, &addr_len);
        add_sse_client(client_socket, addr);

        // Send initial device list
        char json[4096];
        generate_devices_json(json, sizeof(json));
        send_sse_message(client_socket, json);

        // Keep connection alive with periodic heartbeats
        struct timeval tv;
        tv.tv_sec = 30; // 30 second timeout for heartbeat
        tv.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        while (g_running)
        {
            char dummy_buffer[64];
            ssize_t bytes = recv(client_socket, dummy_buffer, sizeof(dummy_buffer), 0);
            if (bytes <= 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // Send heartbeat comment to keep connection alive
                    send(client_socket, ": heartbeat\\n\\n", 14, MSG_NOSIGNAL);
                    continue;
                }
                break; // Client disconnected
            }
        }
        remove_client(client_socket);
        close(client_socket);
        return NULL;
    }

    // Handle HTTP routes
    if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0)
    {
        int is_head_request = strcmp(method, "HEAD") == 0;
        if (strcmp(path, "/") == 0)
        {
            if (is_head_request)
            {
                send_http_response(client_socket, 200, "OK", "text/html", "");
            }
            else
            {
                char html[16384];
                generate_html_page(html, sizeof(html));
                send_http_response(client_socket, 200, "OK", "text/html", html);
            }
        }
        else if (strcmp(path, "/favicon.ico") == 0)
        {
            // Decode embedded favicon
            static unsigned char decoded_favicon[2048];
            static int favicon_len = -1;

            if (favicon_len == -1)
            {
                favicon_len = base64_decode(EMBEDDED_FAVICON, decoded_favicon, sizeof(decoded_favicon));
            }

            if (is_head_request)
            {
                char response_header[256];
                snprintf(response_header, sizeof(response_header),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: image/x-icon\r\n"
                         "Content-Length: %d\r\n"
                         "Cache-Control: public, max-age=86400\r\n"
                         "\r\n",
                         favicon_len);
                send(client_socket, response_header, strlen(response_header), 0);
            }
            else
            {
                char response_header[256];
                snprintf(response_header, sizeof(response_header),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: image/x-icon\r\n"
                         "Content-Length: %d\r\n"
                         "Cache-Control: public, max-age=86400\r\n"
                         "\r\n",
                         favicon_len);
                send(client_socket, response_header, strlen(response_header), 0);
                send(client_socket, decoded_favicon, favicon_len, 0);
            }
        }
        else if (strcmp(path, "/api/devices") == 0)
        {
            if (is_head_request)
            {
                send_http_response(client_socket, 200, "OK", "application/json", "");
            }
            else
            {
                char json[4096];
                generate_devices_json(json, sizeof(json));
                send_http_response(client_socket, 200, "OK", "application/json", json);
            }
        }
        else
        {
            send_http_response(client_socket, 404, "Not Found", "text/plain", "404 Not Found");
        }
    }
    else if (strcmp(method, "POST") == 0)
    {
        // Extract JSON body
        char *body = strstr(buffer, "\r\n\r\n");
        if (body)
        {
            body += 4;

            if (strcmp(path, "/bind") == 0)
            {
                // Extract busid from JSON {"busid":"1-1.1"}
                char *busid_start = strstr(body, "\"busid\":\"");
                if (busid_start)
                {
                    busid_start += 9;
                    char *busid_end = strchr(busid_start, '"');
                    if (busid_end)
                    {
                        *busid_end = '\0';
                        char cmd[256];
                        char output[1024] = {0};
                        // Redirect stderr to stdout to capture error messages
                        snprintf(cmd, sizeof(cmd), "usbip bind -b %s 2>&1", busid_start);
                        log_message("INFO", "Attempting to bind device: %s", busid_start);
                        
                        if (exec_command(cmd, output, sizeof(output)) == 0)
                        {
                            log_message("INFO", "Successfully bound device: %s", busid_start);
                            send_http_response(client_socket, 200, "OK", "application/json", "{\"status\":\"success\"}");
                        }
                        else
                        {
                            log_message("ERROR", "Failed to bind device: %s", busid_start);
                            char error_response[2048];
                            // Clean up the error message
                            char *clean_output = output;
                            while (*clean_output == '\n' || *clean_output == '\r' || *clean_output == ' ') clean_output++;
                            if (strlen(clean_output) == 0) strcpy(clean_output, "Unknown error");
                            snprintf(error_response, sizeof(error_response), "{\"status\":\"failed\",\"error\":\"%s\"}", clean_output);
                            send_http_response(client_socket, 500, "Internal Server Error", "application/json", error_response);
                        }
                        // Trigger immediate update
                        list_usbip_devices();
                        broadcast_devices_update();
                    }
                }
            }
            else if (strcmp(path, "/unbind") == 0)
            {
                // Extract busid from JSON
                char *busid_start = strstr(body, "\"busid\":\"");
                if (busid_start)
                {
                    busid_start += 9;
                    char *busid_end = strchr(busid_start, '"');
                    if (busid_end)
                    {
                        *busid_end = '\0';
                        char cmd[256];
                        char output[1024] = {0};
                        // Redirect stderr to stdout to capture error messages
                        snprintf(cmd, sizeof(cmd), "usbip unbind -b %s 2>&1", busid_start);
                        log_message("INFO", "Attempting to unbind device: %s", busid_start);
                        
                        if (exec_command(cmd, output, sizeof(output)) == 0)
                        {
                            log_message("INFO", "Successfully unbound device: %s", busid_start);
                            send_http_response(client_socket, 200, "OK", "application/json", "{\"status\":\"success\"}");
                        }
                        else
                        {
                            log_message("ERROR", "Failed to unbind device: %s", busid_start);
                            char error_response[2048];
                            // Clean up the error message
                            char *clean_output = output;
                            while (*clean_output == '\n' || *clean_output == '\r' || *clean_output == ' ') clean_output++;
                            if (strlen(clean_output) == 0) strcpy(clean_output, "Unknown error");
                            snprintf(error_response, sizeof(error_response), "{\"status\":\"failed\",\"error\":\"%s\"}", clean_output);
                            send_http_response(client_socket, 500, "Internal Server Error", "application/json", error_response);
                        }
                        // Trigger immediate update
                        list_usbip_devices();
                        broadcast_devices_update();
                    }
                }
            }
            else
            {
                send_http_response(client_socket, 404, "Not Found", "text/plain", "404 Not Found");
            }
        }
    }
    else
    {
        send_http_response(client_socket, 405, "Method Not Allowed", "text/plain", "405 Method Not Allowed");
    }

    close(client_socket);
    return NULL;
}

// Main server loop
void *server_thread(void *arg)
{
    (void)arg; // Unused parameter
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Socket creation failed");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(g_config.bind_address);
    server_addr.sin_port = htons(g_config.port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(server_socket);
        exit(1);
    }

    if (listen(server_socket, 10) < 0)
    {
        perror("Listen failed");
        close(server_socket);
        exit(1);
    }

    // Get and display startup information
    char *local_ip = get_local_ip();
    printf("\n🚀 usbctl v%s server started successfully!\n", VERSION);
    printf("📡 Server: %s:%d\n", g_config.bind_address, g_config.port);
    printf("🌐 Web interface URLs:\n");
    printf("   http://localhost:%d\n", g_config.port);
    if (strcmp(local_ip, "localhost") != 0)
    {
        printf("   http://%s:%d\n", local_ip, g_config.port);
    }
    if (strcmp(g_config.bind_address, "0.0.0.0") != 0 && strcmp(g_config.bind_address, local_ip) != 0)
    {
        printf("   http://%s:%d\n", g_config.bind_address, g_config.port);
    }
    printf("📊 Status: Ready for connections\n");
    printf("📝 Logs: %s\n", g_config.verbose_logging ? g_config.log_file : "disabled");
    printf("\n⚠️  Press Ctrl+C to stop the server gracefully\n");
    printf("💡 Or send SIGTERM signal to process for clean shutdown\n\n");

    log_message("INFO", "Server started on %s:%d", g_config.bind_address, g_config.port);
    log_message("INFO", "Web interface available at multiple URLs");

    while (g_running)
    {
        // Use select with timeout to make accept interruptible
        fd_set readfds;
        struct timeval timeout;

        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        timeout.tv_sec = 1; // 1 second timeout
        timeout.tv_usec = 0;

        int ready = select(server_socket + 1, &readfds, NULL, NULL, &timeout);

        if (ready < 0)
        {
            if (errno == EINTR)
            {
                // Signal interrupted select - check if we should exit
                continue;
            }
            if (g_running)
            {
                perror("Select failed");
            }
            break;
        }

        if (ready == 0)
        {
            // Timeout - continue loop to check g_running
            continue;
        }

        if (!FD_ISSET(server_socket, &readfds))
        {
            continue;
        }

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (client_socket < 0)
        {
            if (g_running)
                perror("Accept failed");
            continue;
        }

        // Create thread to handle client
        pthread_t client_thread;
        int *socket_ptr = malloc(sizeof(int));
        *socket_ptr = client_socket;

        if (pthread_create(&client_thread, NULL, handle_client, socket_ptr) != 0)
        {
            perror("Thread creation failed");
            close(client_socket);
            free(socket_ptr);
        }
        else
        {
            pthread_detach(client_thread);
        }
    }

    close(server_socket);
    return NULL;
}

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

void print_usage()
{
    printf("usbctl v%s - USB/IP Device Web Manager\n", VERSION);
    printf("Author: %s\n\n", AUTHOR);
    printf("Usage: usbctl [OPTIONS] [COMMANDS]\n\n");
    printf("Web Server Options:\n");
    printf("  -p, --port PORT        Server port (default: %d)\n", DEFAULT_PORT);
    printf("  -b, --bind ADDRESS     Bind address (default: %s)\n", DEFAULT_BIND);
    printf("  -i, --interval SEC     Polling interval (default: 3)\n");
    printf("  -c, --config PATH      Configuration file path\n");
    printf("  -v, --verbose          Enable verbose logging\n");
    printf("  -q, --quiet            Disable logging output\n");
    printf("  -d, --daemon           Run as daemon (background)\n\n");
    printf("Commands:\n");
    printf("  --list                 List USB devices (JSON format)\n");
    printf("  --bind BUSID           Bind USB device\n");
    printf("  --unbind BUSID         Unbind USB device\n");
    printf("  --init-config          Create default configuration\n");
    printf("  --print-config         Show current configuration\n");
    printf("  --install-service      Install systemd service\n");
    printf("  --version              Show version information\n");
    printf("  --help                 Show this help\n\n");
    printf("Examples:\n");
    printf("  usbctl                 # Start web server (best practice defaults)\n");
    printf("  usbctl -p 8080         # Start server on port 8080\n");
    printf("  usbctl -v              # Start with verbose logging\n");
    printf("  usbctl --list          # List available USB devices\n");
    printf("  usbctl --bind 1-1.1    # Bind specific device\n\n");
    printf("Default configuration follows convention over configuration principle.\n");
}

void print_version()
{
    printf("usbctl version %s\n", VERSION);
    printf("USB/IP device web manager for Linux\n");
    printf("Author: %s\n", AUTHOR);
    printf("\nFeatures:\n");
    printf("  * Web-based device management\n");
    printf("  * Real-time status updates\n");
    printf("  * ARM Linux optimized\n");
    printf("  * Zero-dependency deployment\n");
}

void print_config()
{
    printf("Current configuration:\n");
    printf("  Port: %d\n", g_config.port);
    printf("  Bind Address: %s\n", g_config.bind_address);
    printf("  Poll Interval: %d seconds\n", g_config.poll_interval);
    printf("  Config File: %s\n", g_config.config_path);
}

int install_systemd_service()
{
    // Get current executable path
    char exe_path[512];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1)
    {
        perror("Failed to get executable path");
        return 1;
    }
    exe_path[len] = '\0';

    // Create service file content
    const char *service_content =
        "[Unit]\n"
        "Description=usbctl - USB/IP Web Manager\n"
        "After=network.target\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "User=root\n"
        "ExecStart=%s\n"
        "Restart=always\n"
        "RestartSec=5\n"
        "\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n";

    char service_file[1024];
    snprintf(service_file, sizeof(service_file), service_content, exe_path);

    // Write service file
    FILE *fp = fopen("/etc/systemd/system/usbctl.service", "w");
    if (!fp)
    {
        perror("Failed to create service file (run as root)");
        return 1;
    }

    fprintf(fp, "%s", service_file);
    fclose(fp);

    printf("Systemd service installed successfully!\n");
    printf("Enable and start with:\n");
    printf("  sudo systemctl enable usbctl\n");
    printf("  sudo systemctl start usbctl\n");

    return 0;
}

// Signal handler for graceful shutdown
void signal_handler(int sig)
{
    static int shutdown_count = 0;

    shutdown_count++;
    if (shutdown_count == 1)
    {
        if (sig == SIGINT)
        {
            printf("\n\n⏹️  Received interrupt signal (Ctrl+C)\n");
        }
        else
        {
            printf("\n\n⏹️  Received termination signal (%d)\n", sig);
        }
        printf("🔄 Shutting down usbctl server gracefully...\n");

        g_running = 0;

        // Close all client connections immediately
        for (int i = 0; i < g_client_count; i++)
        {
            if (g_clients[i].socket > 0)
            {
                close(g_clients[i].socket);
                g_clients[i].socket = -1;
            }
        }
        g_client_count = 0;

        printf("✅ Server stopped successfully.\n");
        fflush(stdout);
        exit(0); // Exit immediately without delay
    }
    else
    {
        printf("\n💥 Force exit after multiple signals\n");
        _exit(1); // Force exit without cleanup
    }
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main(int argc, char *argv[])
{
    init_config();

    // Parse command line arguments first to check for immediate commands
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            print_usage();
            return 0;
        }
        else if (strcmp(argv[i], "--version") == 0)
        {
            print_version();
            return 0;
        }
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0)
        {
            g_config.verbose_logging = 1;
        }
        else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0)
        {
            g_config.verbose_logging = 0;
        }
        else if (strcmp(argv[i], "--list") == 0)
        {
            // Initialize logging for single command
            init_logging();
            if (!check_system_compatibility())
            {
                fprintf(stderr, "System compatibility check failed\n");
                return 1;
            }

            list_usbip_devices();
            char json[4096];
            generate_devices_json(json, sizeof(json));
            printf("%s\n", json);
            return 0;
        }
        else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc)
        {
            init_logging();
            if (!check_system_compatibility())
            {
                fprintf(stderr, "System compatibility check failed\n");
                return 1;
            }

            if (bind_device(argv[++i]))
            {
                printf("Device bound successfully\n");
                return 0;
            }
            else
            {
                printf("Failed to bind device\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--unbind") == 0 && i + 1 < argc)
        {
            init_logging();
            if (!check_system_compatibility())
            {
                fprintf(stderr, "System compatibility check failed\n");
                return 1;
            }

            if (unbind_device(argv[++i]))
            {
                printf("Device unbound successfully\n");
                return 0;
            }
            else
            {
                printf("Failed to unbind device\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--init-config") == 0)
        {
            save_config();
            printf("Configuration file created: %s\n", g_config.config_path);
            return 0;
        }
        else if (strcmp(argv[i], "--print-config") == 0)
        {
            load_config();
            print_config();
            return 0;
        }
        else if (strcmp(argv[i], "--install-service") == 0)
        {
            return install_systemd_service();
        }
        else if (strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0)
        {
            if (++i < argc)
                g_config.port = atoi(argv[i]);
        }
        else if (strcmp(argv[i], "--bind") == 0 || strcmp(argv[i], "-b") == 0)
        {
            if (++i < argc)
                strncpy(g_config.bind_address, argv[i], sizeof(g_config.bind_address) - 1);
        }
        else if (strcmp(argv[i], "--interval") == 0 || strcmp(argv[i], "-i") == 0)
        {
            if (++i < argc)
                g_config.poll_interval = atoi(argv[i]);
        }
        else if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0)
        {
            if (++i < argc)
                strncpy(g_config.config_path, argv[i], sizeof(g_config.config_path) - 1);
        }
    }

    // Load configuration
    load_config();

    // Initialize logging system
    if (!init_logging())
    {
        fprintf(stderr, "Failed to initialize logging system\n");
        return 1;
    }

    // Check system compatibility
    if (!check_system_compatibility())
    {
        fprintf(stderr, "System compatibility check failed\n");
        return 1;
    }

    log_message("INFO", "Starting usbctl v%s", VERSION);
    log_message("INFO", "Author: %s", AUTHOR);

    // Initial device list
    list_usbip_devices();

    // Setup signal handling for graceful shutdown (non-restarting)
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Don't restart interrupted system calls
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN); // Ignore broken pipe signals

    // Mark server as started to reduce verbose logging in polling thread
    g_server_started = 1;

    // Start device polling thread
    pthread_t poll_thread;
    if (pthread_create(&poll_thread, NULL, device_poll_thread, NULL) != 0)
    {
        log_message("ERROR", "Failed to create polling thread");
        return 1;
    }

    // Start server thread (runs in main thread)
    server_thread(NULL);

    // Cleanup
    pthread_join(poll_thread, NULL);

    printf("usbctl server stopped.\n");
    return 0;
}