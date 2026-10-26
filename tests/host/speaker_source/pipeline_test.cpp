// Synthetic PCM and a passive source double; playlist/repeat/routing execute production C++.
#include "speaker_source_media_player.h"
#include <cassert>
#include <iostream>
#include <cmath>
using namespace esphome;
using namespace media_player;
using namespace media_source;
class Source : public MediaSource {
 public:
 bool internal=false, repeats=false, stop_pending=false; int starts=0,stops=0; uint32_t played=0;
 std::vector<std::string> uris; std::vector<MediaSourceCommand> commands;
 bool can_handle(const std::string& uri)const override{return uri.rfind("hatch-library:///",0)==0;}
 bool has_internal_playlist()const override{return internal;}
 bool play_uri(const std::string& uri)override{assert(get_state()==MediaSourceState::IDLE);uris.push_back(uri);++starts;set_state_(MediaSourceState::PLAYING);return true;}
 void handle_command(MediaSourceCommand c)override{commands.push_back(c);if(c==MediaSourceCommand::STOP){++stops;stop_pending=true;}if(c==MediaSourceCommand::REPEAT_ONE)repeats=true;if(c==MediaSourceCommand::REPEAT_OFF)repeats=false;}
 void idle(){stop_pending=false;set_state_(MediaSourceState::IDLE);}
 void notify_audio_played(uint32_t n,int64_t)override{played+=n;}
};
// Model Sendspin while a pending start/group leave does not report IDLE on STOP.
// Only an explicit disconnect releases it; it must not claim stock library URIs.
class SmartSource : public Source {
 public:
 SmartSource(){internal=true;}
 bool can_handle(const std::string& uri)const override{return uri.rfind("sendspin://",0)==0;}
};
struct Fixture {
 speaker_source::SpeakerSourceMediaPlayer player; speaker::Speaker speaker; Source source;
 Fixture(){player.set_speaker(0,&speaker);player.add_media_source(0,&source);player.set_volume_initial(.5);player.set_volume_min(0);player.set_volume_max(1);player.set_volume_increment(.05);player.setup();}
 void ticks(int n=10){while(n--)player.loop();}
 void command(MediaPlayerCommand c){player.make_call().set_command(c).perform();ticks();}
 void play(const std::string& name="rain"){player.make_call().set_media_url("hatch-library:///"+name+".wav").perform();ticks();}
 void pcm(const std::vector<uint8_t>& bytes){size_t n=0;while(n<bytes.size()){auto wrote=source.write_output(bytes.data()+n,bytes.size()-n,1,{});assert(wrote>0);n+=wrote;}}
};

#include "micro_wav/wav_decoder.h"
// Build a known 16-bit PCM WAV, not an approximation of a Hatch asset.
static std::vector<uint8_t> wav_fixture(int channels,std::vector<uint8_t>& pcm){
 std::vector<uint8_t> w;
 auto text=[&](const char* s){w.insert(w.end(),s,s+4);};
 auto u16=[&](uint16_t v){w.push_back(v);w.push_back(v>>8);};
 auto u32=[&](uint32_t v){for(int i=0;i<4;i++)w.push_back(v>>(i*8));};
 for(int i=0;i<5003*channels;i++){uint16_t v=static_cast<uint16_t>((i*7919)^0xa531);pcm.push_back(v);pcm.push_back(v>>8);}
 text("RIFF");u32(36+12+pcm.size());text("WAVE");
 text("JUNK");u32(3);w.insert(w.end(),{11,22,33,0});
 text("fmt ");u32(16);u16(1);u16(channels);u32(16000);u32(32000*channels);u16(channels*2);u16(16);
 text("data");u32(pcm.size());w.insert(w.end(),pcm.begin(),pcm.end());return w;
}
static void real_wav(Fixture& f,size_t chunk,int channels){
 std::vector<uint8_t> original;auto wav=wav_fixture(channels,original);
 micro_wav::WAVDecoder decoder;std::vector<uint8_t> expected;
 f.command(MEDIA_PLAYER_COMMAND_REPEAT_ONE);f.play("synthetic-pcm");
 for(int cycle=0;cycle<3;cycle++){
  decoder.reset();assert(!decoder.is_header_ready());size_t pos=0;bool header=false;int guard=0;
  while(pos<wav.size()){
   size_t end=std::min(wav.size(),pos+chunk);
   while(pos<end){
    uint8_t out[258];size_t consumed=0,samples=0;
    auto result=decoder.decode(wav.data()+pos,end-pos,out,sizeof(out),consumed,samples);
    assert(result>=0);assert(++guard<200000);pos+=consumed;
    if(result==micro_wav::WAV_DECODER_HEADER_READY){header=true;assert(decoder.get_channels()==channels);assert(decoder.get_sample_rate()==16000);assert(decoder.get_bits_per_sample()==16);assert(decoder.get_data_chunk_size()==original.size());}
    if(samples)f.pcm(std::vector<uint8_t>(out,out+samples*decoder.get_bytes_per_output_sample()));
    assert(consumed||samples); // A full fixture cannot stall.
   }
  }
  size_t consumed=0,samples=0;uint8_t out[258];assert(header);
  assert(decoder.decode(nullptr,0,out,sizeof(out),consumed,samples)==micro_wav::WAV_DECODER_END_OF_STREAM);
  assert(decoder.get_bytes_remaining()==0);
  expected.insert(expected.end(),original.begin(),original.end());
  assert(f.speaker.accepted==expected); // Every byte, including final short chunk, exactly once.
  f.speaker.drain();f.source.idle();f.ticks();assert(f.source.starts==cycle+2);
 }
}

int main(int argc,char** argv){assert(argc>=2);std::string mode=argv[1];Fixture f;
 const std::vector<uint8_t> pcm{1,2,3,4,5,6,7,8,9,10,11,12,13,14};
 if(mode=="wav"){assert(argc==4);real_wav(f,std::stoul(argv[2]),std::stoi(argv[3]));
 }else if(mode=="eof_repeat"){
  f.command(MEDIA_PLAYER_COMMAND_REPEAT_ONE);f.play();
  std::vector<uint8_t> expected;
  for(int i=0;i<4;i++){
   f.pcm(pcm);expected.insert(expected.end(),pcm.begin(),pcm.end());f.source.idle();
   f.ticks();assert(f.source.starts==i+1); // Do not restart until speaker drains.
   f.speaker.drain();f.ticks();assert(f.source.starts==i+2);
   assert(f.player.state==MEDIA_PLAYER_STATE_PLAYING);
  }
  assert(f.speaker.accepted==expected);assert(f.source.played==0); // IDLE detaches source before the speaker tail drains.assert(f.source.uris.size()==5);
 }else if(mode=="repeat_flags"){
  Source other,smart;smart.internal=true;f.player.add_media_source(0,&other);f.player.add_media_source(0,&smart);
  f.command(MEDIA_PLAYER_COMMAND_REPEAT_ONE);assert(f.source.repeats&&other.repeats&&smart.commands.empty());
  f.play();f.command(MEDIA_PLAYER_COMMAND_REPEAT_OFF);assert(!f.source.repeats&&!other.repeats);
  f.pcm(pcm);f.speaker.drain();f.source.idle();f.ticks();assert(f.source.starts==1);assert(f.player.state==MEDIA_PLAYER_STATE_IDLE);
  f.command(MEDIA_PLAYER_COMMAND_REPEAT_ONE);f.command(MEDIA_PLAYER_COMMAND_REPEAT_ALL);assert(!f.source.repeats);
 }else if(mode=="smart_to_stock"||mode=="idle_smart_to_stock"){
  Source smart;smart.internal=true;f.player.add_media_source(0,&smart);
  // Sendspin can become active independently of the player's local playlist.
  smart.play_uri("sendspin://session");
  if(mode=="idle_smart_to_stock")smart.idle();
  // Stock Sound queues REPEAT_ONE and then its URI, without a loop() between calls.
  f.player.make_call().set_command(MEDIA_PLAYER_COMMAND_REPEAT_ONE).perform();
  f.player.make_call().set_media_url("hatch-library:///rain.wav").perform();
  f.ticks();if(mode=="smart_to_stock")assert(smart.stop_pending);smart.idle();f.ticks();
  assert(f.source.starts==1);assert(f.source.repeats);
  if(mode=="smart_to_stock") {
   assert(std::find(smart.commands.begin(),smart.commands.end(),MediaSourceCommand::REPEAT_ONE)!=smart.commands.end());
   assert(smart.stops==1);
  }
  f.source.idle();f.ticks();assert(f.source.starts==2);
 }else if(mode=="pending_smart_to_stock"||mode=="pending_stock_reselection"){
  SmartSource smart;f.player.add_media_source(0,&smart);
  smart.play_uri("sendspin://session");
  // A pending Sendspin start may request its own URI while it already owns playback.
  const bool self_start=mode=="pending_smart_to_stock";
  f.player.make_call().set_media_url(self_start?"sendspin://current":"hatch-library:///rain.wav").perform();
  f.ticks();assert(smart.stop_pending&&smart.stops==1);
  assert(smart.get_state()==MediaSourceState::PLAYING&&f.source.starts==0);
  // A stock selection disconnects Sendspin before submitting its replacement URI.
  // Do not give the orchestrator a loop between disconnect and replacement.
  smart.idle();
  const std::string selected="hatch-library:///ocean.wav";
  f.player.make_call().set_command(MEDIA_PLAYER_COMMAND_REPEAT_ONE).perform();
  f.player.make_call().set_media_url(selected).perform();
  f.ticks(100);
  assert(smart.starts==1); // The stale self-request must not reclaim playback.
  assert(f.source.starts==1&&f.source.uris.back()==selected); // No obsolete rain start either.
  assert(f.source.repeats);
  f.pcm(pcm);assert(f.speaker.accepted==pcm);
  f.command(MEDIA_PLAYER_COMMAND_PAUSE);
  assert(!f.source.commands.empty()&&f.source.commands.back()==MediaSourceCommand::PAUSE);
 }else if(mode=="stop_pending_smart"){
  SmartSource smart;f.player.add_media_source(0,&smart);
  smart.play_uri("sendspin://session");
  f.player.make_call().set_media_url("sendspin://current").perform();
  f.ticks();assert(smart.stop_pending&&smart.stops==1&&f.source.starts==0);
  // Stock None disconnects, then requests STOP rather than a replacement URI.
  smart.idle();
  f.command(MEDIA_PLAYER_COMMAND_STOP);f.ticks(100);
  assert(smart.starts==1&&f.source.starts==0);
  assert(f.player.state==MEDIA_PLAYER_STATE_IDLE);
 }else if(mode=="replace_idle_before_loop"){
  f.command(MEDIA_PLAYER_COMMAND_REPEAT_OFF);f.play();f.pcm(pcm);
  f.player.make_call().set_media_url("hatch-library:///ocean.wav").perform();
  // A source task can complete STOP before the main loop handles PLAY_URI.
  assert(f.source.stop_pending&&f.source.stops==1);
  assert(f.speaker.stops==1&&f.speaker.queued.empty());
  f.source.idle();f.ticks(100);
  assert(f.source.starts==2&&f.source.uris.back()=="hatch-library:///ocean.wav");
  assert(f.source.stops==1&&f.speaker.stops==1); // Never send a second STOP.
  f.pcm({21,22,23,24});assert((f.speaker.queued==std::vector<uint8_t>{21,22,23,24}));
 }else if(mode=="replace_preserves_other_pipeline"){
  speaker::Speaker announcement_speaker;Source announcement;
  f.player.set_speaker(1,&announcement_speaker);f.player.add_media_source(1,&announcement);
  f.player.make_call().set_announcement(true).set_media_url("hatch-library:///announcement.wav").perform();
  f.player.make_call().set_announcement(true).set_command(MEDIA_PLAYER_COMMAND_REPEAT_ONE).perform();
  f.player.make_call().set_command(MEDIA_PLAYER_COMMAND_REPEAT_ONE).perform();
  f.player.make_call().set_media_url("hatch-library:///rain.wav").perform();
  // Only the media pipeline's pending selection is superseded.
  f.player.make_call().set_media_url("hatch-library:///ocean.wav").perform();
  f.ticks();
  assert(announcement.starts==1&&announcement.uris.back()=="hatch-library:///announcement.wav");
  assert(f.source.starts==1&&f.source.uris.back()=="hatch-library:///ocean.wav");
  assert(announcement.repeats&&f.source.repeats); // Queued SEND_COMMAND survives for both pipelines.
 }else if(mode=="stop"){
  f.command(MEDIA_PLAYER_COMMAND_REPEAT_ONE);f.play();f.pcm(pcm);f.command(MEDIA_PLAYER_COMMAND_STOP);
  assert(f.source.stop_pending);f.source.idle();f.speaker.drain();f.ticks(100);assert(f.source.starts==1);assert(f.player.state==MEDIA_PLAYER_STATE_IDLE);
 }else if(mode=="replace"){
  f.command(MEDIA_PLAYER_COMMAND_REPEAT_ONE);f.play();f.pcm(pcm);
  f.player.make_call().set_media_url("hatch-library:///ocean.wav").perform();f.ticks();
  assert(f.source.stops==1);assert(f.speaker.stops==1);assert(f.speaker.queued.empty());assert(f.source.starts==1);
  // Late writes from a stopping source cannot add PCM after the IDLE callback.
  f.source.idle();assert(f.source.write_output(pcm.data(),pcm.size(),1,{})==0);
  f.ticks();assert(f.source.starts==2);assert(f.source.uris.back()=="hatch-library:///ocean.wav");
  f.pcm({21,22,23,24});assert((f.speaker.queued==std::vector<uint8_t>{21,22,23,24}));
  f.speaker.drain();f.source.idle();f.ticks();assert(f.source.starts==3);assert(f.source.uris.back()=="hatch-library:///ocean.wav");
 }else if(mode=="volume"){
  f.command(MEDIA_PLAYER_COMMAND_REPEAT_ONE);f.play();f.pcm(pcm);auto queue=f.speaker.queued;
  f.player.make_call().set_volume(0).perform();assert(f.speaker.volume==0&&f.speaker.muted&&f.player.is_muted());
  f.player.make_call().set_volume(.7).perform();assert(f.speaker.volume==.7f&&!f.speaker.muted);
  f.command(MEDIA_PLAYER_COMMAND_VOLUME_UP);assert(std::abs(f.speaker.volume-.75f)<.00001f);
  f.command(MEDIA_PLAYER_COMMAND_MUTE);assert(f.speaker.muted);f.command(MEDIA_PLAYER_COMMAND_UNMUTE);assert(!f.speaker.muted);
  for(int i=0;i<20;i++)f.command(MEDIA_PLAYER_COMMAND_VOLUME_UP);assert(f.speaker.volume==1);
  for(int i=0;i<20;i++)f.command(MEDIA_PLAYER_COMMAND_VOLUME_DOWN);assert(f.speaker.volume==0&&f.speaker.muted);
  assert(f.source.starts==1&&f.source.repeats&&f.speaker.queued==queue&&f.source.stops==0);
  f.speaker.drain();f.source.idle();f.ticks();assert(f.source.starts==2&&f.source.repeats);
 }else if(mode=="repeat_all"){
  f.command(MEDIA_PLAYER_COMMAND_REPEAT_ALL);f.play();f.player.make_call().set_media_url("hatch-library:///ocean.wav").set_command(MEDIA_PLAYER_COMMAND_ENQUEUE).perform();f.ticks();
  f.source.idle();f.ticks();assert(f.source.uris.back()=="hatch-library:///ocean.wav");f.source.idle();f.ticks();assert(f.source.uris.back()=="hatch-library:///rain.wav");assert(f.source.starts==3);
 }else return 2;
 std::cout<<mode<<" passed\n";
}
