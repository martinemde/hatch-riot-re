#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <deque>
namespace esphome {
template<class T> using optional=std::optional<T>;
namespace setup_priority { constexpr float PROCESSOR=1; }
class ESPPreferenceObject { public: template<class T> bool load(T*){return false;} template<class T> void save(T*){} };
class Component { public: virtual void setup(){} virtual void loop(){} virtual void dump_config(){} virtual float get_setup_priority() const{return 0;}
 bool is_ready()const{return true;} void defer(std::function<void()> f){f();}
 std::map<uint32_t,std::function<void()>> timers;
 void set_timeout(uint32_t id,uint32_t,std::function<void()> f){timers[id]=f;} void cancel_timeout(uint32_t id){timers.erase(id);}
};
template<class... T> class Trigger {public: void trigger(T...){} };
template<class A,class B> A remap(A v,A lo,A hi,B a,B b){return a+(v-lo)*(b-a)/(hi-lo);}
inline uint32_t random_uint32(){return 123;}
namespace audio { struct AudioStreamInfo { int rate=16000; bool operator!=(const AudioStreamInfo& o)const{return rate!=o.rate;} uint32_t bytes_to_frames(size_t n)const{return n/2;} }; }
namespace speaker { class Speaker { public:
 audio::AudioStreamInfo info; std::vector<uint8_t> queued,accepted; size_t capacity=4; int stops=0,finishes=0; bool stopped=true; float volume=0; bool muted=false;
 std::function<void(uint32_t,int64_t)> callback;
 void add_audio_output_callback(std::function<void(uint32_t,int64_t)> f){callback=f;}
 audio::AudioStreamInfo get_audio_stream_info(){return info;} void set_audio_stream_info(audio::AudioStreamInfo v){info=v;}
 size_t play(const uint8_t* d,size_t n,uint32_t){n=std::min(n,capacity);queued.insert(queued.end(),d,d+n);accepted.insert(accepted.end(),d,d+n);stopped=false;return n;}
 void drain(){auto n=queued.size();queued.clear();stopped=true;if(callback)callback(n/2,123);}
 void stop(){++stops;queued.clear();stopped=true;} void finish(){++finishes;stopped=queued.empty();} bool is_stopped(){return stopped;}
 void set_volume(float v){volume=v;} void set_mute_state(bool v){muted=v;}
}; }
namespace media_player {
enum MediaPlayerCommand { MEDIA_PLAYER_COMMAND_TOGGLE,MEDIA_PLAYER_COMMAND_PLAY,MEDIA_PLAYER_COMMAND_PAUSE,MEDIA_PLAYER_COMMAND_STOP,MEDIA_PLAYER_COMMAND_NEXT,MEDIA_PLAYER_COMMAND_PREVIOUS,MEDIA_PLAYER_COMMAND_REPEAT_ONE,MEDIA_PLAYER_COMMAND_REPEAT_OFF,MEDIA_PLAYER_COMMAND_REPEAT_ALL,MEDIA_PLAYER_COMMAND_CLEAR_PLAYLIST,MEDIA_PLAYER_COMMAND_SHUFFLE,MEDIA_PLAYER_COMMAND_UNSHUFFLE,MEDIA_PLAYER_COMMAND_ENQUEUE,MEDIA_PLAYER_COMMAND_MUTE,MEDIA_PLAYER_COMMAND_UNMUTE,MEDIA_PLAYER_COMMAND_VOLUME_UP,MEDIA_PLAYER_COMMAND_VOLUME_DOWN };
enum MediaPlayerState {MEDIA_PLAYER_STATE_IDLE,MEDIA_PLAYER_STATE_PLAYING,MEDIA_PLAYER_STATE_PAUSED,MEDIA_PLAYER_STATE_ANNOUNCING};
inline const char* media_player_state_to_string(MediaPlayerState){return "state";}
struct MediaPlayerSupportedFormat{}; enum class MediaPlayerEntityFeature {REPEAT_SET};
class MediaPlayerTraits {public: std::vector<MediaPlayerSupportedFormat> formats; void set_supports_pause(bool){} void add_feature_flags(MediaPlayerEntityFeature){} auto& get_supported_formats(){return formats;} };
class MediaPlayer;
class MediaPlayerCall {public: MediaPlayer* p; optional<std::string> uri; optional<bool> ann; optional<float> vol; optional<MediaPlayerCommand> cmd;
 explicit MediaPlayerCall(MediaPlayer* p):p(p){} auto get_announcement()const{return ann;} const auto& get_media_url()const{return uri;} auto get_volume()const{return vol;} auto get_command()const{return cmd;}
 MediaPlayerCall& set_media_url(const std::string& s){uri=s;return *this;} MediaPlayerCall& set_announcement(bool a){ann=a;return *this;} MediaPlayerCall& set_command(MediaPlayerCommand c){cmd=c;return *this;} MediaPlayerCall& set_volume(float v){vol=v;return *this;} void perform();
};
class MediaPlayer {public: float volume=.5; MediaPlayerState state=MEDIA_PLAYER_STATE_IDLE; virtual MediaPlayerTraits get_traits()=0;virtual bool is_muted()const=0;virtual void control(const MediaPlayerCall&)=0;
 MediaPlayerCall make_call(){return MediaPlayerCall(this);} void publish_state(){} template<class T> ESPPreferenceObject make_entity_preference(){return {};}
};
inline void MediaPlayerCall::perform(){p->control(*this);}
}
}
struct HostQueue {size_t capacity,item_size;std::deque<std::vector<uint8_t>> data;};
using QueueHandle_t=HostQueue*;
inline std::vector<std::unique_ptr<HostQueue>> queues;
inline QueueHandle_t xQueueCreate(size_t n,size_t s){queues.emplace_back(new HostQueue{n,s,{}});return queues.back().get();}
inline int xQueueSend(QueueHandle_t q,const void* p,int){if(q->data.size()==q->capacity)return 0;auto b=static_cast<const uint8_t*>(p);q->data.emplace_back(b,b+q->item_size);return 1;}
inline int xQueuePeek(QueueHandle_t q,void* p,int){if(q->data.empty())return 0;std::memcpy(p,q->data.front().data(),q->item_size);return 1;}
inline int xQueueReceive(QueueHandle_t q,void* p,int x){if(!xQueuePeek(q,p,x))return 0;q->data.pop_front();return 1;}
inline void vTaskDelay(uint32_t){}
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGCONFIG(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGV(tag,...) ((void)(tag))
#define ESP_LOGD(tag,...) ((void)(tag))
