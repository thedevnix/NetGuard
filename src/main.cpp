#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <cstring>
#include <stdexcept>
using Clock=std::chrono::steady_clock;
struct Target {std::string host; int port; std::string severity;};
struct Result {Target target; bool ok; long ms; std::string reason;};
std::string now(){auto t=std::time(nullptr);std::tm tm{};localtime_r(&t,&tm);std::ostringstream s;s<<std::put_time(&tm,"%Y-%m-%d %H:%M:%S");return s.str();}
std::string clean(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return "";auto b=s.find_last_not_of(" \t\r\n");return s.substr(a,b-a+1);}
std::vector<Target> load(const std::string& path){std::ifstream f(path);if(!f)throw std::runtime_error("Cannot open "+path);std::vector<Target> v;std::string line;int n=0;while(std::getline(f,line)){++n;line=clean(line);if(line.empty()||line[0]=='#')continue;std::stringstream ss(line);std::string h,p,s;if(!std::getline(ss,h,',')||!std::getline(ss,p,',')||!std::getline(ss,s,','))throw std::runtime_error("Invalid config line "+std::to_string(n));h=clean(h);p=clean(p);s=clean(s);int port=0;try{size_t used=0;port=std::stoi(p,&used);if(used!=p.size())throw std::invalid_argument("port");}catch(...){throw std::runtime_error("Invalid port at line "+std::to_string(n));}if(h.empty()||port<1||port>65535||(s!="P1"&&s!="P2"&&s!="P3"))throw std::runtime_error("Invalid target at line "+std::to_string(n));v.push_back({h,port,s});}if(v.empty())throw std::runtime_error("No targets in config");return v;}
Result check(Target t,int timeout){auto start=Clock::now();addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;addrinfo* addresses=nullptr;int rc=getaddrinfo(t.host.c_str(),std::to_string(t.port).c_str(),&hints,&addresses);if(rc!=0)return {t,false,0,std::string("DNS: ")+gai_strerror(rc)};bool ok=false;std::string reason="Connection failed";for(auto* a=addresses;a;a=a->ai_next){int fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(fd<0)continue;int flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);continue;}rc=connect(fd,a->ai_addr,a->ai_addrlen);if(rc==0){ok=true;close(fd);break;}if(errno==EINPROGRESS){auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();int remaining=std::max(0,timeout-static_cast<int>(elapsed));pollfd pfd{fd,POLLOUT,0};int pr=poll(&pfd,1,remaining);if(pr>0){int err=0;socklen_t len=sizeof(err);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&len)==0){if(err==0)ok=true;else reason=std::strerror(err);}}else if(pr==0)reason="Timeout";else reason=std::strerror(errno);}else reason=std::strerror(errno);close(fd);if(ok)break;if(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count()>=timeout)break;}freeaddrinfo(addresses);long ms=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();return {t,ok,ms,ok?"Connected":reason};}
std::string csv(std::string s){std::string o="\"";for(char c:s){if(c=='\"')o+="\"\"";else if(c=='\n'||c=='\r')o+=' ';else o+=c;}return o+"\"";}
void append(const std::string& path,const std::string& header,const std::string& record){bool exists=std::filesystem::exists(path)&&std::filesystem::file_size(path)>0;std::ofstream f(path,std::ios::app);if(!f)throw std::runtime_error("Cannot write "+path);if(!exists)f<<header<<'\n';f<<record<<'\n';}
void usage(){std::cout<<"NetGuard: C++17 network monitor\nUsage: netguard [--config PATH] [--interval SECONDS] [--cycles N] [--timeout MS] [--reports DIR]\nDefaults: config/targets.csv, interval=10, cycles=1, timeout=2000, reports=reports\nUse --cycles 0 for continuous monitoring.\n";}
int main(int argc,char**argv){try{std::string config="config/targets.csv",reports="reports";int interval=10,cycles=1,timeout=2000;for(int i=1;i<argc;i++){std::string k=argv[i];if(k=="--help"){usage();return 0;}if(i+1>=argc)throw std::runtime_error("Missing value for "+k);std::string v=argv[++i];if(k=="--config")config=v;else if(k=="--reports")reports=v;else if(k=="--interval")interval=std::stoi(v);else if(k=="--cycles")cycles=std::stoi(v);else if(k=="--timeout")timeout=std::stoi(v);else throw std::runtime_error("Unknown option "+k);}if(interval<1||cycles<0||timeout<1||timeout>60000)throw std::runtime_error("Invalid numeric option");auto targets=load(config);std::filesystem::create_directories(reports);std::map<std::string,int> failures;std::map<std::string,int> active;int nextId=1;std::cout<<"NetGuard monitoring "<<targets.size()<<" targets\n";for(int round=0;cycles==0||round<cycles;round++){std::vector<std::future<Result>> futures;for(auto t:targets)futures.push_back(std::async(std::launch::async,[t,timeout]{return check(t,timeout);}));int healthy=0;for(auto& future:futures){Result r=future.get();std::string key=r.target.host+":"+std::to_string(r.target.port);std::string stamp=now();std::cout<<(r.ok?"[OK]   ":"[FAIL] ")<<key<<" "<<r.ms<<"ms "<<r.reason<<"\n";append(reports+"/checks.csv","timestamp,host,port,healthy,latency_ms,detail",csv(stamp)+","+csv(r.target.host)+","+std::to_string(r.target.port)+","+(r.ok?"1":"0")+","+std::to_string(r.ms)+","+csv(r.reason));if(r.ok){++healthy;failures[key]=0;if(active[key]){append(reports+"/incidents.csv","timestamp,id,host,port,severity,event,detail",csv(stamp)+","+std::to_string(active[key])+","+csv(r.target.host)+","+std::to_string(r.target.port)+","+csv(r.target.severity)+",RESOLVED,"+csv("Service recovered"));std::cout<<"  Incident INC-"<<std::setw(4)<<std::setfill('0')<<active[key]<<std::setfill(' ')<<" RESOLVED\n";active[key]=0;}}else{++failures[key];if(!active[key]&&failures[key]>=2){active[key]=nextId++;append(reports+"/incidents.csv","timestamp,id,host,port,severity,event,detail",csv(stamp)+","+std::to_string(active[key])+","+csv(r.target.host)+","+std::to_string(r.target.port)+","+csv(r.target.severity)+",OPEN,"+csv(r.reason));std::cout<<"  Incident INC-"<<std::setw(4)<<std::setfill('0')<<active[key]<<std::setfill(' ')<<" OPEN ("<<r.target.severity<<")\n";}}}std::cout<<"Summary: "<<healthy<<" healthy / "<<targets.size()-healthy<<" failed\n";if(cycles==0||round+1<cycles)std::this_thread::sleep_for(std::chrono::seconds(interval));}return 0;}catch(const std::exception& e){std::cerr<<"Error: "<<e.what()<<"\n";return 1;}}
