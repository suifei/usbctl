/*
 * usbctl - USB/IP Device Web Manager
 * Security-hardened cross-platform implementation
 *
 * Build Linux:   gcc -O2 -o usbctl usbctl.c -lpthread -Wall -Wextra
 * Build Windows: gcc -O2 -o usbctl.exe usbctl.c -lws2_32 -Wall -Wextra
 * Usage: sudo ./usbctl [options]
 */

// Platform detection
#ifdef _WIN32
#define PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <io.h>
#include <direct.h>
// Link with ws2_32 using -lws2_32 flag instead of pragma
#define close(s) closesocket(s)
#define ssize_t int
#define sleep(x) Sleep((x) * 1000)
#define mkdir(path, mode) _mkdir(path)
#define MSG_NOSIGNAL 0
#define SIGPIPE 13
#define WEXITSTATUS(w) (((w) >> 8) & 0xff)
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_IXUSR  
#define S_IXUSR _S_IEXEC
#endif
typedef void (*sighandler_t)(int);
#define signal_compat(sig, handler) signal(sig, handler)
#else
#define PLATFORM_UNIX
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#define signal_compat(sig, handler) signal(sig, handler)
#endif

// Common headers
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Pthread on Windows requires special handling
#ifdef PLATFORM_WINDOWS
#include <windows.h>
typedef HANDLE pthread_t;
typedef CRITICAL_SECTION pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER {0}
#define pthread_mutex_lock(m) EnterCriticalSection(m)
#define pthread_mutex_unlock(m) LeaveCriticalSection(m)

// Windows thread wrapper to match pthread signature
typedef struct {
    void *(*start_routine)(void *);
    void *arg;
} thread_wrapper_t;

static DWORD WINAPI thread_wrapper(LPVOID param) {
    thread_wrapper_t *wrapper = (thread_wrapper_t *)param;
    void *(*start)(void *) = wrapper->start_routine;
    void *arg = wrapper->arg;
    free(wrapper);
    start(arg);
    return 0;
}

static inline int pthread_create(pthread_t *thread, void *attr, void *(*start)(void *), void *arg) {
    (void)attr;
    thread_wrapper_t *wrapper = malloc(sizeof(thread_wrapper_t));
    if (!wrapper) return -1;
    wrapper->start_routine = start;
    wrapper->arg = arg;
    *thread = CreateThread(NULL, 0, thread_wrapper, wrapper, 0, NULL);
    return *thread ? 0 : -1;
}
static inline int pthread_join(pthread_t thread, void **retval) {
    (void)retval;
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}
static inline int pthread_detach(pthread_t thread) {
    CloseHandle(thread);
    return 0;
}
#else
#include <pthread.h>
#endif

#define VERSION "1.0.0"
#define AUTHOR "github.com/suifei"
#define DEFAULT_PORT 11980
#define DEFAULT_BIND "0.0.0.0"
#define MAX_CLIENTS 10
#define BUFFER_SIZE 8192
#define CONFIG_PATH_SIZE 512
#define MAX_DEVICES 32
#define MAX_LSUSB_ENTRIES 64
#define LOG_BUFFER_SIZE 1024
#define JSON_BUFFER_SIZE 8192

// Configuration structure
typedef struct {
    int port;
    char bind_address[64];
    int poll_interval;
    char config_path[CONFIG_PATH_SIZE];
    int verbose_logging;
    char log_file[256];
    char bound_devices[MAX_DEVICES][16];
    int bound_devices_count;
} config_t;

// USB device structure
typedef struct {
    char busid[16];
    char info[256];
    int bound;
} usb_device_t;

#ifndef PLATFORM_WINDOWS
// Structure to map USB VID:PID to human-readable description (Linux only)
typedef struct {
    char id[16];
    char desc[256];
} lsusb_entry_t;
#endif

// SSE client structure
typedef struct {
    int socket;
    int is_sse;
    struct sockaddr_in addr;
    time_t last_heartbeat;
} client_t;

// Global variables
static config_t g_config = {DEFAULT_PORT, DEFAULT_BIND, 3, "", 1, "/var/log/usbctl.log", {""}, 0};
static usb_device_t g_devices[MAX_DEVICES];
#ifndef PLATFORM_WINDOWS
static lsusb_entry_t g_lsusb_map[MAX_LSUSB_ENTRIES];
static int g_lsusb_count = 0;
#endif
static int g_device_count = 0;
static client_t g_clients[MAX_CLIENTS];
static int g_client_count = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_running = 1;
static volatile int g_server_started = 0;
static FILE *g_log_file = NULL;
static int g_usbip_error_shown = 0;

// Forward declarations
void update_bound_devices_config(void);
void restore_bound_devices(void);
int bind_device(const char *busid);
int unbind_device(const char *busid);
void send_http_response(int client_socket, int status_code, const char *status_text, 
                       const char *content_type, const char *body);

// ============================================================================
// EMBEDDED WEB RESOURCES
// ============================================================================

const char *LOGO_SVG = "<svg width=\"64\" height=\"64\" viewBox=\"0 0 64 64\" "
                       "xmlns=\"http://www.w3.org/2000/svg\">"
                       "<rect x=\"12\" y=\"24\" width=\"24\" height=\"16\" rx=\"2\" "
                       "fill=\"#007BFF\" stroke=\"#0056b3\" stroke-width=\"2\"/>"
                       "<rect x=\"36\" y=\"28\" width=\"4\" height=\"2\" fill=\"#fff\"/>"
                       "<rect x=\"36\" y=\"32\" width=\"4\" height=\"2\" fill=\"#fff\"/>"
                       "<path d=\"M40 32 L52 32\" stroke=\"#007BFF\" stroke-width=\"3\" "
                       "stroke-linecap=\"round\"/>"
                       "<circle cx=\"52\" cy=\"32\" r=\"6\" fill=\"#28a745\" "
                       "stroke=\"#1e7e34\" stroke-width=\"2\"/>"
                       "<circle cx=\"52\" cy=\"32\" r=\"2\" fill=\"#fff\"/>"
                       "</svg>";

static const char *EMBEDDED_FAVICON = "AAABAAEAEBAAAAEAIABoBAAAFgAAACgAAAAQAAAAIAAAAAEAIAAAAAAAAAQAABILAAASCwAAAAAAAAAAAAD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wD///8A////AP///wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wD///8A////AP///wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wD///8A////AP///wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wD///8A////AP///wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wAHtP8AB7T/AAe0/wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wAoqAAAs+0AAP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////ACoqKgAoqAAAKKgAAP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////ACoqKgAoqAAAKKgAAP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////ACoqKgAoqAAAKKgAAP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////ACoqKgAoqAAAKKgAAP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////ACoqKgAoqAAAKKgAAP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////ACoqKgAoqAAA////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8A////AP///wD///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

static const char *EMBEDDED_CSS = "*{margin:0;padding:0;box-sizing:border-box}"
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

const char *EMBEDDED_JS = "let eventSource,devices=[],lang='en',logEntries=[];"
                          "const i18n={'en':{'title':'USB/IP Manager','device':'Device Info','busid':'Bus ID','status':'Status','action':'Action','bound':'Bound','unbound':'Unbound','bind':'Bind','unbind':'Unbind','connected':'Connected','disconnected':'Disconnected','error':'Error','author':'Author','log_title':'Operation Log','clear':'Clear','bind_success':'Device {busid} bound successfully','unbind_success':'Device {busid} unbound successfully','bind_error':'Error binding device {busid}: {error}','unbind_error':'Error unbinding device {busid}: {error}'},'zh':{'title':'USB/IP 管理器','device':'设备信息','busid':'总线ID','status':'状态','action':'操作','bound':'已绑定','unbound':'未绑定','bind':'绑定','unbind':'解绑','connected':'已连接','disconnected':'已断开','error':'错误','author':'作者','log_title':'操作日志','clear':'清除','bind_success':'设备 {busid} 绑定成功','unbind_success':'设备 {busid} 解绑成功','bind_error':'绑定设备 {busid} 失败: {error}','unbind_error':'解绑设备 {busid} 失败: {error}'}};"
                          "function detectLang(){try{const stored=localStorage.getItem('usbctl_lang');if(stored&&i18n[stored])return stored;const nav=navigator.language||navigator.userLanguage||navigator.browserLanguage||'en';const langCode=nav.toLowerCase();if(langCode.startsWith('zh')||langCode.includes('chinese')||langCode.includes('cn'))return 'zh';return 'en';}catch(e){return 'en';}}"
                          "function t(k,vars){let text=i18n[lang][k]||k;if(vars){Object.keys(vars).forEach(key=>{text=text.replace(`{${key}}`,vars[key]);})}return text;}"
                          "function setLang(l){lang=l;localStorage.setItem('usbctl_lang',l);updateUI();}"
                          "function updateUI(){document.title=`usbctl - ${t('title')}`;document.querySelector('h1').textContent=t('title');document.documentElement.lang=lang==='zh'?'zh-CN':'en';const ths=document.querySelectorAll('th');if(ths.length>=4){ths[0].textContent=t('device');ths[1].textContent=t('busid');ths[2].textContent=t('status');ths[3].textContent=t('action');}document.querySelectorAll('.lang-btn').forEach(b=>b.classList.toggle('active',b.dataset.lang===lang));const logTitle=document.querySelector('.log-title');if(logTitle)logTitle.textContent=t('log_title');const clearBtn=document.querySelector('.clear-log-btn');if(clearBtn)clearBtn.textContent=t('clear');render();renderLog();}"
                          "function connectSSE(){if(eventSource)eventSource.close();eventSource=new EventSource('/events');eventSource.onopen=()=>document.getElementById('status').textContent=t('connected');eventSource.onmessage=e=>{try{devices=JSON.parse(e.data);render();}catch(err){console.error(err);}};eventSource.onerror=()=>{document.getElementById('status').textContent=t('disconnected');setTimeout(connectSSE,3000)};}"
                          "function render(){const tbody=document.querySelector('tbody');tbody.innerHTML=devices.map(d=>`<tr><td>${d.info}</td><td><code>${d.busid}</code></td>`+`<td><span class=\"status ${d.bound?'bound':'unbound'}\">${t(d.bound?'bound':'unbound')}</span></td>`+`<td><button class=\"${d.bound?'btn-unbind':'btn-bind'}\" onclick=\"toggle('${d.busid}')\">`+`${t(d.bound?'unbind':'bind')}</button></td></tr>`).join('');}"
                          "function addLog(type,message){const timestamp=new Date().toLocaleTimeString();const entry={type,message,timestamp};logEntries.unshift(entry);if(logEntries.length>100)logEntries.pop();renderLog();}"
                          "function renderLog(){const logContent=document.getElementById('logContent');if(!logContent)return;logContent.innerHTML=logEntries.map(entry=>`<div class=\"log-entry log-${entry.type}\">`+`<span class=\"log-timestamp\">[${entry.timestamp}]</span> ${entry.message}`+`</div>`).join('');logContent.scrollTop=0;}"
                          "function clearLog(){logEntries=[];renderLog();}"
                          "function toggle(busid){const device=devices.find(d=>d.busid===busid);if(!device)return;const button=event.target;const action=device.bound?'unbind':'bind';button.disabled=true;fetch(`/${action}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({busid})}).then(response=>{if(response.ok){return response.json().then(data=>{addLog('success',t(`${action}_success`,{busid}));if(data.devices){devices=data.devices;render();}else{loadDevices();}});}else{return response.text().then(text=>{let errorMsg='Unknown error';try{const data=JSON.parse(text);if(data.error){errorMsg=data.error.trim();const errorMatch=errorMsg.match(/error[:\\s]*(.+?)(?:\\n|$)/i);if(errorMatch)errorMsg=errorMatch[1];}}catch(e){const errorMatch=text.match(/error[:\\s]*(.+?)(?:\\n|$)/i);if(errorMatch)errorMsg=errorMatch[1];}addLog('error',t(`${action}_error`,{busid,error:errorMsg}));throw new Error(errorMsg);});}}).catch(err=>{console.error(err);}).finally(()=>button.disabled=false);}"
                          "function loadDevices(){fetch('/api/devices').then(r=>r.json()).then(data=>{devices=data;render()}).catch(console.error);}"
                          "window.onload=()=>{lang=detectLang();updateUI();connectSSE();loadDevices();};";

const char *EMBEDDED_HTML = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
                            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,user-scalable=no\">"
                            "<title>usbctl - USB/IP Manager</title><style>%s</style></head><body>"
                            "<header><div class=\"logo\">%s</div><h1>USB/IP Manager</h1></header>"
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
// UTILITY FUNCTIONS
// ============================================================================

// Safe string length with maximum bound
static size_t safe_strnlen(const char *s, size_t maxlen) {
    if (!s) return 0;
    const char *p = s;
    while (maxlen-- > 0 && *p) p++;
    return p - s;
}

// Initialize logging system
int init_logging(void) {
    if (!g_config.verbose_logging) {
        g_log_file = fopen("/dev/null", "w");
        return g_log_file != NULL;
    }

    g_log_file = fopen(g_config.log_file, "a");
    if (!g_log_file) {
        g_log_file = stderr;
        fprintf(stderr, "[WARN] Cannot open log file, using stderr\n");
    }
    return 1;
}

// Log message with timestamp - format string vulnerability fixed
void log_message(const char *level, const char *format, ...) {
    if (!g_log_file || !level || !format)
        return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    // Sanitize level parameter
    char safe_level[16];
    size_t level_len = safe_strnlen(level, sizeof(safe_level) - 1);
    if (level_len >= sizeof(safe_level)) {
        memcpy(safe_level, "TOOLONG", 7);
        safe_level[7] = '\0';
    } else {
        memcpy(safe_level, level, level_len);
        safe_level[level_len] = '\0';
    }

    fprintf(g_log_file, "[%s] %s: ", timestamp, safe_level);

    va_list args;
    va_start(args, format);
    vfprintf(g_log_file, format, args);
    va_end(args);

    fprintf(g_log_file, "\n");
    fflush(g_log_file);
}

// Validate busid format
static int validate_busid(const char *busid) {
    if (!busid) return 0;
    
    size_t len = safe_strnlen(busid, 64);
    if (len == 0 || len >= 64) return 0;
    
    // busid format: number-number.number (e.g., "1-1.2")
    for (size_t i = 0; i < len; i++) {
        char c = busid[i];
        if (!((c >= '0' && c <= '9') || c == '-' || c == '.')) {
            return 0;
        }
    }
    return 1;
}

// Validate command for allowed list
static int validate_command(const char *cmd) {
    if (!cmd) return 0;
    
    // Platform-specific allowed commands
#ifdef PLATFORM_WINDOWS
    const char *allowed_commands[] = {
        "usbipd", "usbip", NULL
    };
#else
    const char *allowed_commands[] = {
        "usbip", "lsusb", "modprobe", NULL
    };
#endif
    
    for (int i = 0; allowed_commands[i]; i++) {
        size_t cmd_len = strlen(allowed_commands[i]);
        if (strncmp(cmd, allowed_commands[i], cmd_len) == 0) {
            // Ensure next char is space or null
            if (cmd[cmd_len] == ' ' || cmd[cmd_len] == '\0') {
                return 1;
            }
        }
    }
    
    return 0;
}

// Secure command execution
static int secure_exec_command(const char *cmd, char *output, size_t output_size) {
    if (!cmd || !output || output_size == 0) {
        log_message("ERROR", "Invalid arguments");
        return -1;
    }
    
    if (!validate_command(cmd)) {
        log_message("ERROR", "Command not allowed");
        return -1;
    }
    
    memset(output, 0, output_size);
    
#ifdef PLATFORM_WINDOWS
    // Windows: Use CreateProcess for secure execution
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        log_message("ERROR", "Failed to create pipe");
        return -1;
    }
    
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;
    
    char cmd_copy[1024];
    size_t cmd_len = safe_strnlen(cmd, sizeof(cmd_copy) - 1);
    memcpy(cmd_copy, cmd, cmd_len);
    cmd_copy[cmd_len] = '\0';
    
    if (!CreateProcessA(NULL, cmd_copy, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        log_message("ERROR", "Failed to execute command");
        return -1;
    }
    
    CloseHandle(hWrite);
    
    DWORD bytesRead;
    size_t totalRead = 0;
    char buffer[256];
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        if (totalRead + bytesRead < output_size - 1) {
            memcpy(output + totalRead, buffer, bytesRead);
            totalRead += bytesRead;
        } else {
            break;
        }
    }
    output[totalRead] = '\0';
    
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    
    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return (int)exitCode;
#else
    // Unix/Linux: Use fork/exec for secure execution
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        log_message("ERROR", "Pipe creation failed");
        return -1;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        log_message("ERROR", "Fork failed");
        return -1;
    }
    
    if (pid == 0) {
        // Child process
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        
        // Parse command safely
        char *args[16];
        int argc = 0;
        char cmd_copy[256];
        size_t cmd_len = safe_strnlen(cmd, sizeof(cmd_copy) - 1);
        memcpy(cmd_copy, cmd, cmd_len);
        cmd_copy[cmd_len] = '\0';
        
        char *token = strtok(cmd_copy, " ");
        while (token && argc < 15) {
            args[argc++] = token;
            token = strtok(NULL, " ");
        }
        args[argc] = NULL;
        
        execvp(args[0], args);
        _exit(127);
    }
    
    // Parent process
    close(pipefd[1]);
    
    size_t total = 0;
    char buffer[256];
    ssize_t bytesRead;
    
    while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
        if (total + (size_t)bytesRead < output_size - 1) {
            memcpy(output + total, buffer, bytesRead);
            total += bytesRead;
        } else {
            break;
        }
    }
    output[total] = '\0';
    
    close(pipefd[0]);
    
    int status;
    waitpid(pid, &status, 0);
    
    return WEXITSTATUS(status);
#endif
}

// Check if device is bound
int is_device_bound(const char *busid) {
    if (!validate_busid(busid)) {
        return 0;
    }
    
#ifdef PLATFORM_WINDOWS
    // On Windows, we need to check via usbipd command
    // For now, return 0 (not bound by default)
    // Full implementation would query usbipd state
    (void)busid;
    return 0;
#else
    char path[256];
    int ret = snprintf(path, sizeof(path), "/sys/bus/usb/drivers/usbip-host/%s", busid);
    if (ret < 0 || ret >= (int)sizeof(path)) {
        return 0;
    }
    
    struct stat st;
    return (stat(path, &st) == 0);
#endif
}

// Get local IP address
char *get_local_ip(void) {
    static char ip_str[INET_ADDRSTRLEN] = "localhost";
    
#ifdef PLATFORM_WINDOWS
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return ip_str;
    }
#endif
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
#ifdef PLATFORM_WINDOWS
        WSACleanup();
#endif
        return ip_str;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr("223.5.5.5");
    dest.sin_port = htons(80);

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) == 0) {
        struct sockaddr_in local;
        socklen_t local_len = sizeof(local);
        if (getsockname(sock, (struct sockaddr *)&local, &local_len) == 0) {
            inet_ntop(AF_INET, &local.sin_addr, ip_str, INET_ADDRSTRLEN);
        }
    }
    close(sock);
    
#ifdef PLATFORM_WINDOWS
    WSACleanup();
#endif
    
    return ip_str;
}

// Initialize default configuration
void init_config(void) {
#ifdef PLATFORM_WINDOWS
    char appdata[MAX_PATH];
    if (GetEnvironmentVariableA("LOCALAPPDATA", appdata, sizeof(appdata)) > 0) {
        snprintf(g_config.config_path, sizeof(g_config.config_path), 
                "%s\\usbctl\\config", appdata);
    } else {
        snprintf(g_config.config_path, sizeof(g_config.config_path), 
                "C:\\ProgramData\\usbctl\\config");
    }
    snprintf(g_config.log_file, sizeof(g_config.log_file), 
            "C:\\ProgramData\\usbctl\\usbctl.log");
#else
    int ret = snprintf(g_config.config_path, sizeof(g_config.config_path), 
                      "/etc/usbctl/config");
    if (ret < 0 || ret >= (int)sizeof(g_config.config_path)) {
        log_message("WARN", "Config path truncated");
    }
#endif
}

// Create directory recursively
int mkdirs(const char *path) {
    if (!path) return -1;
    
    char tmp[512];
    int ret = snprintf(tmp, sizeof(tmp), "%s", path);
    if (ret < 0 || ret >= (int)sizeof(tmp)) return -1;
    
    size_t len = strlen(tmp);
#ifdef PLATFORM_WINDOWS
    if (tmp[len - 1] == '\\' || tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            char sep = *p;
            *p = '\0';
            mkdir(tmp, 0755);
            *p = sep;
        }
    }
#else
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
#endif
    return mkdir(tmp, 0755);
}

// Load configuration
int load_config(void) {
    FILE *fp = fopen(g_config.config_path, "r");
    if (!fp) return 0;

    char line[256];
    g_config.bound_devices_count = 0;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';

        if (strncmp(line, "port=", 5) == 0) {
            g_config.port = atoi(line + 5);
        } else if (strncmp(line, "bind=", 5) == 0) {
            size_t len = safe_strnlen(line + 5, sizeof(g_config.bind_address) - 1);
            memcpy(g_config.bind_address, line + 5, len);
            g_config.bind_address[len] = '\0';
        } else if (strncmp(line, "poll_interval=", 14) == 0) {
            g_config.poll_interval = atoi(line + 14);
        } else if (strncmp(line, "verbose_logging=", 16) == 0) {
            g_config.verbose_logging = atoi(line + 16);
        } else if (strncmp(line, "log_file=", 9) == 0) {
            size_t len = safe_strnlen(line + 9, sizeof(g_config.log_file) - 1);
            memcpy(g_config.log_file, line + 9, len);
            g_config.log_file[len] = '\0';
        } else if (strncmp(line, "bound_device=", 13) == 0 && 
                   g_config.bound_devices_count < MAX_DEVICES) {
            size_t len = safe_strnlen(line + 13, 15);
            memcpy(g_config.bound_devices[g_config.bound_devices_count], line + 13, len);
            g_config.bound_devices[g_config.bound_devices_count][len] = '\0';
            g_config.bound_devices_count++;
        }
    }
    fclose(fp);
    return 1;
}

// Save configuration
int save_config(void) {
    char dir_path[512];
    int ret = snprintf(dir_path, sizeof(dir_path), "%s", g_config.config_path);
    if (ret < 0 || ret >= (int)sizeof(dir_path)) return 0;
    
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdirs(dir_path);
    }

    FILE *fp = fopen(g_config.config_path, "w");
    if (!fp) return 0;

    fprintf(fp, "port=%d\n", g_config.port);
    fprintf(fp, "bind=%s\n", g_config.bind_address);
    fprintf(fp, "poll_interval=%d\n", g_config.poll_interval);

    update_bound_devices_config();
    for (int i = 0; i < g_config.bound_devices_count; i++) {
        if (strlen(g_config.bound_devices[i]) > 0) {
            fprintf(fp, "bound_device=%s\n", g_config.bound_devices[i]);
        }
    }

    fclose(fp);
    return 1;
}

// ============================================================================
// USB/IP BACKEND FUNCTIONS
// ============================================================================

// Update bound devices configuration
void update_bound_devices_config(void) {
    g_config.bound_devices_count = 0;
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].bound && g_config.bound_devices_count < MAX_DEVICES) {
            size_t len = safe_strnlen(g_devices[i].busid, 15);
            memcpy(g_config.bound_devices[g_config.bound_devices_count], 
                   g_devices[i].busid, len);
            g_config.bound_devices[g_config.bound_devices_count][len] = '\0';
            g_config.bound_devices_count++;
        }
    }
}

// Restore bound devices
void restore_bound_devices(void) {
    log_message("INFO", "Restoring bound devices");

    for (int i = 0; i < g_config.bound_devices_count; i++) {
        if (strlen(g_config.bound_devices[i]) > 0) {
            if (bind_device(g_config.bound_devices[i])) {
                log_message("INFO", "Restored: %s", g_config.bound_devices[i]);
            }
        }
    }
}

#ifndef PLATFORM_WINDOWS
// Parse lsusb output (Linux only)
static void parse_lsusb(const char *output) {
    g_lsusb_count = 0;
    if (!output) return;

    char buf[8192];
    size_t len = safe_strnlen(output, sizeof(buf) - 1);
    memcpy(buf, output, len);
    buf[len] = '\0';

    char *line = strtok(buf, "\n");
    while (line && g_lsusb_count < MAX_LSUSB_ENTRIES) {
        char *id_pos = strstr(line, "ID ");
        if (id_pos) {
            id_pos += 3;
            char *colon = strchr(id_pos, ':');
            if (colon && (colon - id_pos == 4)) {
                char vid[5] = {0}, pid[5] = {0};
                memcpy(vid, id_pos, 4);
                memcpy(pid, colon + 1, 4);

                char *desc_start = colon + 5;
                while (*desc_start == ' ') desc_start++;

                if (*desc_start) {
                    snprintf(g_lsusb_map[g_lsusb_count].id, 
                            sizeof(g_lsusb_map[0].id), "%s:%s", vid, pid);
                    size_t desc_len = safe_strnlen(desc_start, 
                                                   sizeof(g_lsusb_map[0].desc) - 1);
                    memcpy(g_lsusb_map[g_lsusb_count].desc, desc_start, desc_len);
                    g_lsusb_map[g_lsusb_count].desc[desc_len] = '\0';
                    g_lsusb_count++;
                }
            }
        }
        line = strtok(NULL, "\n");
    }
}
#endif

// List USB devices
int list_usbip_devices(void) {
    char output[4096];

#ifndef PLATFORM_WINDOWS
    // Step 1: Try to get lsusb data (Linux only)
    char lsusb_output[8192];
    g_lsusb_count = 0;
    if (secure_exec_command("lsusb", lsusb_output, sizeof(lsusb_output)) == 0) {
        parse_lsusb(lsusb_output);
    }
#endif

    // Step 2: Try usbip commands
#ifdef PLATFORM_WINDOWS
    const char *usbip_commands[] = {
        "usbipd wsl list",
        "usbipd list",
        "usbip list -l",
        NULL
    };
#else
    const char *usbip_commands[] = {
        "usbip list -l",
        "/usr/bin/usbip list -l",
        "/usr/sbin/usbip list -l",
        NULL
    };
#endif

    int cmd_success = 0;
    for (int i = 0; usbip_commands[i] != NULL; i++) {
        if (secure_exec_command(usbip_commands[i], output, sizeof(output)) == 0) {
            cmd_success = 1;
            break;
        }
    }

    if (!cmd_success) {
        if (!g_usbip_error_shown) {
            log_message("ERROR", "Failed to execute usbip");
            g_usbip_error_shown = 1;
        }
        return 0;
    }

    g_device_count = 0;
    char buf[4096];
    size_t len = safe_strnlen(output, sizeof(buf) - 1);
    memcpy(buf, output, len);
    buf[len] = '\0';

    char *line = strtok(buf, "\n");
    usb_device_t *current = NULL;

    while (line && g_device_count < MAX_DEVICES) {
        char *original_line = line;
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (trimmed[0] == '\0') {
            line = strtok(NULL, "\n");
            continue;
        }

        if (strncmp(trimmed, "- busid", 7) == 0 || strncmp(trimmed, "BUSID", 5) == 0) {
            current = &g_devices[g_device_count++];
            memset(current, 0, sizeof(usb_device_t));

            char *busid_start = strstr(trimmed, "busid ");
            if (!busid_start) busid_start = strstr(trimmed, "BUSID");
            if (busid_start) {
                busid_start += (strncmp(busid_start, "BUSID", 5) == 0) ? 5 : 6;
            } else {
                busid_start = trimmed + 8;
            }

            while (busid_start && *busid_start == ' ') busid_start++;
            
            if (busid_start) {
                char *end = strpbrk(busid_start, " \t(");
                size_t busid_len;
                if (end) {
                    busid_len = (end - busid_start < 15) ? (end - busid_start) : 15;
                } else {
                    busid_len = safe_strnlen(busid_start, 15);
                }
                memcpy(current->busid, busid_start, busid_len);
                current->busid[busid_len] = '\0';
                current->bound = is_device_bound(current->busid);
            }
        } else if (current && (original_line[0] == ' ' || original_line[0] == '\t')) {
            size_t current_len = strlen(current->info);
            size_t avail = sizeof(current->info) - current_len - 1;
            if (current_len > 0 && avail > 0) {
                current->info[current_len] = ' ';
                current_len++;
                avail--;
            }
            if (avail > 0) {
                size_t copy_len = safe_strnlen(trimmed, avail);
                memcpy(current->info + current_len, trimmed, copy_len);
                current->info[current_len + copy_len] = '\0';
            }
        }

        line = strtok(NULL, "\n");
    }

#ifndef PLATFORM_WINDOWS
    // Enhance with lsusb data on Linux
    for (int i = 0; i < g_device_count; i++) {
        if (strstr(g_devices[i].info, "unknown vendor")) {
            char *paren = strchr(g_devices[i].info, '(');
            if (paren) {
                char vidpid[10] = {0};
                size_t vidpid_len = safe_strnlen(paren + 1, 9);
                memcpy(vidpid, paren + 1, vidpid_len);
                vidpid[vidpid_len] = '\0';
                char *close_paren = strchr(vidpid, ')');
                if (close_paren) *close_paren = '\0';

                for (int j = 0; j < g_lsusb_count; j++) {
                    if (strcmp(g_lsusb_map[j].id, vidpid) == 0) {
                        size_t desc_len = safe_strnlen(g_lsusb_map[j].desc, 
                                                       sizeof(g_devices[i].info) - 1);
                        memcpy(g_devices[i].info, g_lsusb_map[j].desc, desc_len);
                        g_devices[i].info[desc_len] = '\0';
                        break;
                    }
                }
            }
        }
    }
#endif

    return g_device_count;
}

// Bind USB device
int bind_device(const char *busid) {
    if (!validate_busid(busid)) {
        log_message("ERROR", "Invalid busid");
        return 0;
    }

    char cmd[256];
    char output[1024] = {0};
    
#ifdef PLATFORM_WINDOWS
    int ret = snprintf(cmd, sizeof(cmd), "usbipd wsl attach --busid %s", busid);
#else
    int ret = snprintf(cmd, sizeof(cmd), "usbip bind -b %s", busid);
#endif
    
    if (ret < 0 || ret >= (int)sizeof(cmd)) {
        log_message("ERROR", "Command too long");
        return 0;
    }

    log_message("INFO", "Binding device: %s", busid);
    int result = (secure_exec_command(cmd, output, sizeof(output)) == 0);
    
    if (result) {
        log_message("INFO", "Successfully bound: %s", busid);
    } else {
        log_message("ERROR", "Failed to bind: %s", busid);
    }

    return result;
}

// Unbind USB device
int unbind_device(const char *busid) {
    if (!validate_busid(busid)) {
        log_message("ERROR", "Invalid busid");
        return 0;
    }

    char cmd[256];
    char output[1024] = {0};
    
#ifdef PLATFORM_WINDOWS
    int ret = snprintf(cmd, sizeof(cmd), "usbipd wsl detach --busid %s", busid);
#else
    int ret = snprintf(cmd, sizeof(cmd), "usbip unbind -b %s", busid);
#endif
    
    if (ret < 0 || ret >= (int)sizeof(cmd)) {
        log_message("ERROR", "Command too long");
        return 0;
    }

    log_message("INFO", "Unbinding device: %s", busid);
    int result = secure_exec_command(cmd, output, sizeof(output)) == 0;

    if (result) {
        log_message("INFO", "Successfully unbound: %s", busid);
    } else {
        log_message("ERROR", "Failed to unbind: %s", busid);
    }

    return result;
}

// ============================================================================
// HTTP SERVER FUNCTIONS
// ============================================================================

// Generate HTML page
void generate_html_page(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;
    
    int ret = snprintf(buffer, buffer_size, EMBEDDED_HTML, 
                      EMBEDDED_CSS, LOGO_SVG, EMBEDDED_JS);
    if (ret < 0 || ret >= (int)buffer_size) {
        log_message("WARN", "HTML page truncated");
    }
}

// Send HTTP response
void send_http_response(int client_socket, int status_code, const char *status_text,
                       const char *content_type, const char *body) {
    if (client_socket < 0) return;
    
    const char *safe_status_text = status_text ? status_text : "Unknown";
    const char *safe_content_type = content_type ? content_type : "text/plain";
    
    char header[1024];
    int body_len = body ? safe_strnlen(body, 1024*1024) : 0;

    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "\r\n",
        status_code, safe_status_text, safe_content_type, body_len);

    if (header_len >= (int)sizeof(header)) return;

    send(client_socket, header, header_len, 0);
    if (body && body_len > 0) {
        send(client_socket, body, body_len, 0);
    }
}

// Generate devices JSON
void generate_devices_json(char *buffer, size_t buffer_size) {
    if (buffer_size == 0) return;
    
    size_t pos = 0;
    buffer[pos++] = '[';
    
    for (int i = 0; i < g_device_count && pos < buffer_size - 1; i++) {
        // Sanitize info field
        char info_sanitized[256];
        size_t k = 0;
        for (size_t j = 0; g_devices[i].info[j] && k < sizeof(info_sanitized) - 1; j++) {
            unsigned char c = (unsigned char)g_devices[i].info[j];
            if (c == '"' || c == '\\') {
                if (k + 2 >= sizeof(info_sanitized) - 1) break;
                info_sanitized[k++] = '\\';
                info_sanitized[k++] = c;
            } else if (c >= 32 && c < 127) {
                info_sanitized[k++] = c;
            }
        }
        info_sanitized[k] = '\0';

        char device_json[384];
        int written = snprintf(device_json, sizeof(device_json),
            "%s{\"busid\":\"%s\",\"info\":\"%s\",\"bound\":%s}",
            i > 0 ? "," : "", g_devices[i].busid, info_sanitized,
            g_devices[i].bound ? "true" : "false");
            
        if (written > 0 && pos + written < buffer_size - 1) {
            memcpy(buffer + pos, device_json, written);
            pos += written;
        }
    }
    
    if (pos < buffer_size - 1) {
        buffer[pos++] = ']';
    }
    buffer[pos] = '\0';
}

// Send SSE message
ssize_t send_sse_message(int client_socket, const char *data) {
    char response[4096];
    size_t data_len = safe_strnlen(data, sizeof(response) - 10);
    
    memcpy(response, "data: ", 6);
    memcpy(response + 6, data, data_len);
    response[6 + data_len] = '\n';
    response[7 + data_len] = '\n';
    response[8 + data_len] = '\0';
    
    return send(client_socket, response, 8 + data_len, MSG_NOSIGNAL);
}

// Send SSE headers
void send_sse_headers(int client_socket) {
    const char *headers = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/event-stream\r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: keep-alive\r\n"
                          "Access-Control-Allow-Origin: *\r\n"
                          "\r\n";
    send(client_socket, headers, strlen(headers), MSG_NOSIGNAL);
}

// Base64 decode
static int base64_decode(const char *input, unsigned char *output, int max_len) {
    const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int len = 0, i = 0, j = 0;
    unsigned char temp[4];

    while (input[i] && len < max_len - 3) {
        if (input[i] == '=' || input[i] == '\n' || input[i] == '\r' || input[i] == ' ') {
            i++;
            continue;
        }

        const char *p = strchr(table, input[i]);
        if (!p) {
            i++;
            continue;
        }
        temp[j++] = p - table;

        if (j == 4) {
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
void add_sse_client(int socket, struct sockaddr_in addr) {
    pthread_mutex_lock(&g_mutex);
    if (g_client_count < MAX_CLIENTS) {
        g_clients[g_client_count].socket = socket;
        g_clients[g_client_count].is_sse = 1;
        g_clients[g_client_count].addr = addr;
        g_clients[g_client_count].last_heartbeat = time(NULL);
        g_client_count++;
    }
    pthread_mutex_unlock(&g_mutex);
}

// Remove client
void remove_client(int socket) {
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_client_count; i++) {
        if (g_clients[i].socket == socket) {
            g_clients[i] = g_clients[g_client_count - 1];
            g_client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex);
}

// Broadcast devices update
void broadcast_devices_update(void) {
    char *json = malloc(JSON_BUFFER_SIZE);
    if (!json) return;
    
    generate_devices_json(json, JSON_BUFFER_SIZE);

    pthread_mutex_lock(&g_mutex);
    for (int i = g_client_count - 1; i >= 0; i--) {
        if (g_clients[i].is_sse) {
            ssize_t result = send_sse_message(g_clients[i].socket, json);
            if (result <= 0) {
                close(g_clients[i].socket);
                if (i < g_client_count - 1) {
                    g_clients[i] = g_clients[g_client_count - 1];
                }
                g_client_count--;
            } else {
                g_clients[i].last_heartbeat = time(NULL);
            }
        }
    }
    pthread_mutex_unlock(&g_mutex);
    free(json);
}

// ============================================================================
// SERVER THREADS
// ============================================================================

// Device polling thread
void *device_poll_thread(void *arg) {
    (void)arg;
    usb_device_t prev_devices[MAX_DEVICES];
    int prev_count = 0;

    while (g_running) {
        pthread_mutex_lock(&g_mutex);
        memcpy(prev_devices, g_devices, sizeof(prev_devices));
        prev_count = g_device_count;
        pthread_mutex_unlock(&g_mutex);

        list_usbip_devices();

        int changed = 0;
        if (prev_count != g_device_count) {
            changed = 1;
        } else {
            for (int i = 0; i < g_device_count; i++) {
                if (strcmp(prev_devices[i].busid, g_devices[i].busid) != 0 ||
                    prev_devices[i].bound != g_devices[i].bound) {
                    changed = 1;
                    break;
                }
            }
        }

        if (changed) {
            broadcast_devices_update();
        }

        sleep(g_config.poll_interval);
    }
    return NULL;
}

// Handle client request
void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    ssize_t received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

    if (received <= 0) {
        close(client_socket);
        return NULL;
    }

    buffer[received] = '\0';

    // Parse HTTP request
    char method[16], path[256], version[16];
    if (sscanf(buffer, "%15s %255s %15s", method, path, version) != 3) {
        close(client_socket);
        return NULL;
    }

    // Handle SSE events endpoint
    if (strcmp(path, "/events") == 0 && strcmp(method, "GET") == 0) {
        send_sse_headers(client_socket);

        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        getpeername(client_socket, (struct sockaddr *)&addr, &addr_len);
        add_sse_client(client_socket, addr);

        char *json = malloc(JSON_BUFFER_SIZE);
        if (!json) {
            close(client_socket);
            return NULL;
        }
        generate_devices_json(json, JSON_BUFFER_SIZE);
        send_sse_message(client_socket, json);
        free(json);

#ifdef PLATFORM_WINDOWS
        // Windows: timeout in milliseconds
        DWORD timeout = 30000; // 30 seconds
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#else
        // Unix: timeout as struct timeval
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

        while (g_running) {
            char dummy_buffer[64];
            ssize_t bytes = recv(client_socket, dummy_buffer, sizeof(dummy_buffer), 0);
            if (bytes <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    send(client_socket, ": heartbeat\n\n", 13, MSG_NOSIGNAL);
                    continue;
                }
                break;
            }
        }
        remove_client(client_socket);
        close(client_socket);
        return NULL;
    }

    // Handle HTTP routes
    if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) {
        int is_head = strcmp(method, "HEAD") == 0;
        
        if (strcmp(path, "/") == 0) {
            if (is_head) {
                send_http_response(client_socket, 200, "OK", "text/html", "");
            } else {
                char *html = malloc(16384);
                if (html) {
                    generate_html_page(html, 16384);
                    send_http_response(client_socket, 200, "OK", "text/html", html);
                    free(html);
                }
            }
        } else if (strcmp(path, "/favicon.ico") == 0) {
            static unsigned char decoded_favicon[2048];
            static int favicon_len = -1;

            if (favicon_len == -1) {
                favicon_len = base64_decode(EMBEDDED_FAVICON, decoded_favicon, sizeof(decoded_favicon));
            }

            if (is_head) {
                char response_header[512];
                int header_len = snprintf(response_header, sizeof(response_header),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: image/x-icon\r\n"
                    "Content-Length: %d\r\n"
                    "Cache-Control: public, max-age=86400\r\n"
                    "\r\n", favicon_len);
                
                if (header_len > 0 && header_len < (int)sizeof(response_header)) {
                    send(client_socket, response_header, header_len, 0);
                }
            } else {
                char response_header[256];
                int header_len = snprintf(response_header, sizeof(response_header),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: image/x-icon\r\n"
                    "Content-Length: %d\r\n"
                    "Cache-Control: public, max-age=86400\r\n"
                    "\r\n", favicon_len);
                    
                if (header_len > 0 && header_len < (int)sizeof(response_header)) {
                    send(client_socket, response_header, header_len, 0);
                    send(client_socket, (const char *)decoded_favicon, favicon_len, 0);
                }
            }
        } else if (strcmp(path, "/api/devices") == 0) {
            if (!is_head) {
                char *json = malloc(JSON_BUFFER_SIZE);
                if (json) {
                    generate_devices_json(json, JSON_BUFFER_SIZE);
                    send_http_response(client_socket, 200, "OK", "application/json", json);
                    free(json);
                }
            } else {
                send_http_response(client_socket, 200, "OK", "application/json", "");
            }
        } else {
            send_http_response(client_socket, 404, "Not Found", "text/plain", "404 Not Found");
        }
    } else if (strcmp(method, "POST") == 0) {
        char *body = strstr(buffer, "\r\n\r\n");
        if (body && (body - buffer < (ssize_t)strlen(buffer) - 4)) {
            body += 4;
            size_t body_len = safe_strnlen(body, 4096);
            
            if (body_len > 0 && body_len < 4096) {
                if (strcmp(path, "/bind") == 0 || strcmp(path, "/unbind") == 0) {
                    char *busid_start = strstr(body, "\"busid\":\"");
                    if (busid_start) {
                        busid_start += 9;
                        char *busid_end = strchr(busid_start, '"');
                        if (busid_end && busid_end - busid_start < 16) {
                            char busid[16];
                            size_t busid_len = busid_end - busid_start;
                            memcpy(busid, busid_start, busid_len);
                            busid[busid_len] = '\0';
                            
                            int is_bind = strcmp(path, "/bind") == 0;
                            int result = is_bind ? bind_device(busid) : unbind_device(busid);
                            
                            if (result) {
                                list_usbip_devices();
                                save_config();
                                
                                char *response_json = malloc(8192);
                                if (response_json) {
                                    char *devices_json = malloc(4096);
                                    if (devices_json) {
                                        generate_devices_json(devices_json, 4096);
                                        snprintf(response_json, 8192, 
                                                "{\"status\":\"success\",\"devices\":%s}", 
                                                devices_json);
                                        send_http_response(client_socket, 200, "OK", 
                                                         "application/json", response_json);
                                        free(devices_json);
                                    }
                                    free(response_json);
                                }
                                broadcast_devices_update();
                            } else {
                                send_http_response(client_socket, 500, "Internal Server Error",
                                                 "application/json", 
                                                 "{\"status\":\"failed\",\"error\":\"Operation failed\"}");
                            }
                        }
                    }
                } else {
                    send_http_response(client_socket, 404, "Not Found", "text/plain", "404 Not Found");
                }
            }
        }
    } else {
        send_http_response(client_socket, 405, "Method Not Allowed", "text/plain", 
                          "405 Method Not Allowed");
    }

    close(client_socket);
    return NULL;
}

// Main server loop
void *server_thread(void *arg) {
    (void)arg;
    
#ifdef PLATFORM_WINDOWS
    // Initialize Windows Sockets
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return NULL;
    }
#endif
    
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
#ifdef PLATFORM_WINDOWS
        WSACleanup();
#endif
        exit(1);
    }

    int opt = 1;
#ifdef PLATFORM_WINDOWS
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(g_config.bind_address);
    server_addr.sin_port = htons(g_config.port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
#ifdef PLATFORM_WINDOWS
        WSACleanup();
#endif
        exit(1);
    }

    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        close(server_socket);
#ifdef PLATFORM_WINDOWS
        WSACleanup();
#endif
        exit(1);
    }

    char *local_ip = get_local_ip();
    printf("\nServer started on %s:%d\n", g_config.bind_address, g_config.port);
    printf("Web interface: http://%s:%d\n", local_ip, g_config.port);
    printf("Press Ctrl+C to stop\n\n");

    while (g_running) {
        fd_set readfds;
        struct timeval timeout;

        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(server_socket + 1, &readfds, NULL, NULL, &timeout);

        if (ready < 0) {
#ifdef PLATFORM_WINDOWS
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            if (g_running) perror("Select failed");
            break;
        }

        if (ready == 0) continue;
        if (!FD_ISSET(server_socket, &readfds)) continue;

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (client_socket < 0) {
            if (g_running) perror("Accept failed");
            continue;
        }

        pthread_t client_thread;
        int *socket_ptr = malloc(sizeof(int));
        if (!socket_ptr) {
            close(client_socket);
            continue;
        }
        *socket_ptr = client_socket;

        if (pthread_create(&client_thread, NULL, handle_client, socket_ptr) != 0) {
            perror("Thread creation failed");
            close(client_socket);
            free(socket_ptr);
        } else {
            pthread_detach(client_thread);
        }
    }

    close(server_socket);
    
#ifdef PLATFORM_WINDOWS
    WSACleanup();
#endif
    
    return NULL;
}

// ============================================================================
// MAIN
// ============================================================================

void print_usage(void) {
    printf("usbctl v%s - USB/IP Device Web Manager\n", VERSION);
    printf("Author: %s\n\n", AUTHOR);
    printf("Usage: usbctl [OPTIONS]\n\n");
    printf("Options:\n");
    printf("  -p, --port PORT        Server port (default: %d)\n", DEFAULT_PORT);
    printf("  -b, --bind ADDRESS     Bind address (default: %s)\n", DEFAULT_BIND);
    printf("  -i, --interval SEC     Polling interval (default: 3)\n");
    printf("  -c, --config PATH      Configuration file path\n");
    printf("  -v, --verbose          Enable verbose logging\n");
    printf("  --version              Show version\n");
    printf("  --help                 Show this help\n\n");
    printf("Examples:\n");
    printf("  usbctl                 # Start web server\n");
    printf("  usbctl -p 8080         # Start on port 8080\n");
    printf("  usbctl -v              # Start with verbose logging\n");
}

void signal_handler(int sig) {
    (void)sig; // Suppress unused parameter warning
    static int shutdown_count = 0;
    shutdown_count++;
    
    if (shutdown_count == 1) {
        printf("\nShutting down gracefully...\n");
        g_running = 0;
        
        for (int i = 0; i < g_client_count; i++) {
            if (g_clients[i].socket > 0) {
                close(g_clients[i].socket);
                g_clients[i].socket = -1;
            }
        }
        g_client_count = 0;
        
        printf("Server stopped\n");
        fflush(stdout);
        exit(0);
    } else {
        printf("\nForce exit\n");
        _exit(1);
    }
}

int main(int argc, char *argv[]) {
#ifdef PLATFORM_WINDOWS
    // Initialize critical section for Windows
    InitializeCriticalSection(&g_mutex);
#endif
    
    init_config();
    load_config();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("usbctl version %s\n", VERSION);
            return 0;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            g_config.verbose_logging = 1;
        } else if (strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0) {
            if (++i < argc) g_config.port = atoi(argv[i]);
        } else if (strcmp(argv[i], "--bind") == 0 || strcmp(argv[i], "-b") == 0) {
            if (++i < argc) {
                size_t len = safe_strnlen(argv[i], sizeof(g_config.bind_address) - 1);
                memcpy(g_config.bind_address, argv[i], len);
                g_config.bind_address[len] = '\0';
            }
        } else if (strcmp(argv[i], "--interval") == 0 || strcmp(argv[i], "-i") == 0) {
            if (++i < argc) g_config.poll_interval = atoi(argv[i]);
        } else if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) {
            if (++i < argc) {
                size_t len = safe_strnlen(argv[i], sizeof(g_config.config_path) - 1);
                memcpy(g_config.config_path, argv[i], len);
                g_config.config_path[len] = '\0';
            }
        }
    }

    if (!init_logging()) {
        fprintf(stderr, "Failed to initialize logging\n");
        return 1;
    }

    log_message("INFO", "Starting usbctl v%s", VERSION);

    list_usbip_devices();

    if (g_config.bound_devices_count > 0) {
        restore_bound_devices();
        list_usbip_devices();
    }

#ifndef PLATFORM_WINDOWS
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
#else
    signal_compat(SIGINT, signal_handler);
    signal_compat(SIGTERM, signal_handler);
#endif

    g_server_started = 1;

    pthread_t poll_thread;
    if (pthread_create(&poll_thread, NULL, device_poll_thread, NULL) != 0) {
        log_message("ERROR", "Failed to create polling thread");
        return 1;
    }

    server_thread(NULL);

    pthread_join(poll_thread, NULL);

#ifdef PLATFORM_WINDOWS
    DeleteCriticalSection(&g_mutex);
#endif

    return 0;
}