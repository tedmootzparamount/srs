/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_app_statistic.hpp>

#include <unistd.h>
#include <sstream>
using namespace std;

#include <srs_rtmp_stack.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>

int64_t srs_gvid = 0;

int64_t srs_generate_id()
{
    if (srs_gvid == 0) {
        srs_gvid = getpid() * 3;
    }
    return srs_gvid++;
}

SrsStatisticVhost::SrsStatisticVhost()
{
    id = srs_generate_id();
    
    clk = new SrsWallClock();
    kbps = new SrsKbps(clk);
    kbps->set_io(NULL, NULL);
    
    nb_clients = 0;
    nb_streams = 0;
}

SrsStatisticVhost::~SrsStatisticVhost()
{
    srs_freep(kbps);
    srs_freep(clk);
}

srs_error_t SrsStatisticVhost::dumps(SrsJsonObject* obj)
{
    srs_error_t err = srs_success;
    
    // dumps the config of vhost.
    bool hls_enabled = _srs_config->get_hls_enabled(vhost);
    bool enabled = _srs_config->get_vhost_enabled(vhost);
    
    obj->set("id", SrsJsonAny::integer(id));
    obj->set("name", SrsJsonAny::str(vhost.c_str()));
    obj->set("enabled", SrsJsonAny::boolean(enabled));
    obj->set("clients", SrsJsonAny::integer(nb_clients));
    obj->set("streams", SrsJsonAny::integer(nb_streams));
    obj->set("send_bytes", SrsJsonAny::integer(kbps->get_send_bytes()));
    obj->set("recv_bytes", SrsJsonAny::integer(kbps->get_recv_bytes()));
    
    SrsJsonObject* okbps = SrsJsonAny::object();
    obj->set("kbps", okbps);
    
    okbps->set("recv_30s", SrsJsonAny::integer(kbps->get_recv_kbps_30s()));
    okbps->set("send_30s", SrsJsonAny::integer(kbps->get_send_kbps_30s()));
    
    SrsJsonObject* hls = SrsJsonAny::object();
    obj->set("hls", hls);
    
    hls->set("enabled", SrsJsonAny::boolean(hls_enabled));
    if (hls_enabled) {
        hls->set("fragment", SrsJsonAny::number(srsu2msi(_srs_config->get_hls_fragment(vhost))/1000.0));
    }
    
    return err;
}

SrsStatisticStream::SrsStatisticStream()
{
    id = srs_generate_id();
    vhost = NULL;
    active = false;
    connection_cid = -1;
    
    has_video = false;
    vcodec = SrsVideoCodecIdReserved;
    avc_profile = SrsAvcProfileReserved;
    avc_level = SrsAvcLevelReserved;
    
    has_audio = false;
    acodec = SrsAudioCodecIdReserved1;
    asample_rate = SrsAudioSampleRateReserved;
    asound_type = SrsAudioChannelsReserved;
    aac_object = SrsAacObjectTypeReserved;
    width = 0;
    height = 0;
    
    clk = new SrsWallClock();
    kbps = new SrsKbps(clk);
    kbps->set_io(NULL, NULL);
    
    nb_clients = 0;
    nb_frames = 0;
}

SrsStatisticStream::~SrsStatisticStream()
{
    srs_freep(kbps);
    srs_freep(clk);
}

srs_error_t SrsStatisticStream::dumps(SrsJsonObject* obj)
{
    srs_error_t err = srs_success;
    
    obj->set("id", SrsJsonAny::integer(id));
    obj->set("name", SrsJsonAny::str(stream.c_str()));
    obj->set("vhost", SrsJsonAny::integer(vhost->id));
    obj->set("app", SrsJsonAny::str(app.c_str()));
    obj->set("live_ms", SrsJsonAny::integer(srsu2ms(srs_get_system_time())));
    obj->set("clients", SrsJsonAny::integer(nb_clients));
    obj->set("frames", SrsJsonAny::integer(nb_frames));
    obj->set("send_bytes", SrsJsonAny::integer(kbps->get_send_bytes()));
    obj->set("recv_bytes", SrsJsonAny::integer(kbps->get_recv_bytes()));
    
    SrsJsonObject* okbps = SrsJsonAny::object();
    obj->set("kbps", okbps);
    
    okbps->set("recv_30s", SrsJsonAny::integer(kbps->get_recv_kbps_30s()));
    okbps->set("send_30s", SrsJsonAny::integer(kbps->get_send_kbps_30s()));
    
    SrsJsonObject* publish = SrsJsonAny::object();
    obj->set("publish", publish);
    
    publish->set("active", SrsJsonAny::boolean(active));
    publish->set("cid", SrsJsonAny::integer(connection_cid));
    
    if (!has_video) {
        obj->set("video", SrsJsonAny::null());
    } else {
        SrsJsonObject* video = SrsJsonAny::object();
        obj->set("video", video);
        
        video->set("codec", SrsJsonAny::str(srs_video_codec_id2str(vcodec).c_str()));
        video->set("profile", SrsJsonAny::str(srs_avc_profile2str(avc_profile).c_str()));
        video->set("level", SrsJsonAny::str(srs_avc_level2str(avc_level).c_str()));
        video->set("width", SrsJsonAny::integer(width));
        video->set("height", SrsJsonAny::integer(height));
    }
    
    if (!has_audio) {
        obj->set("audio", SrsJsonAny::null());
    } else {
        SrsJsonObject* audio = SrsJsonAny::object();
        obj->set("audio", audio);
        
        audio->set("codec", SrsJsonAny::str(srs_audio_codec_id2str(acodec).c_str()));
        audio->set("sample_rate", SrsJsonAny::integer(srs_flv_srates[asample_rate]));
        audio->set("channel", SrsJsonAny::integer(asound_type + 1));
        audio->set("profile", SrsJsonAny::str(srs_aac_object2str(aac_object).c_str()));
    }
    
    return err;
}

void SrsStatisticStream::publish(int cid)
{
    connection_cid = cid;
    active = true;
    
    vhost->nb_streams++;
}

void SrsStatisticStream::close()
{
    has_video = false;
    has_audio = false;
    active = false;
    
    vhost->nb_streams--;
}

SrsStatisticClient::SrsStatisticClient()
{
    id = 0;
    stream = NULL;
    conn = NULL;
    req = NULL;
    type = SrsRtmpConnUnknown;
    create = srs_get_system_time();
}

SrsStatisticClient::~SrsStatisticClient()
{
}

srs_error_t SrsStatisticClient::dumps(SrsJsonObject* obj)
{
    srs_error_t err = srs_success;
    
    obj->set("id", SrsJsonAny::integer(id));
    obj->set("vhost", SrsJsonAny::integer(stream->vhost->id));
    obj->set("stream", SrsJsonAny::integer(stream->id));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("pageUrl", SrsJsonAny::str(req->pageUrl.c_str()));
    obj->set("swfUrl", SrsJsonAny::str(req->swfUrl.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("url", SrsJsonAny::str(req->get_stream_url().c_str()));
    obj->set("type", SrsJsonAny::str(srs_client_type_string(type).c_str()));
    obj->set("publish", SrsJsonAny::boolean(srs_client_type_is_publish(type)));
    obj->set("alive", SrsJsonAny::number(srsu2ms(srs_get_system_time() - create) / 1000.0));
    
    return err;
}

SrsStatisticCategory::SrsStatisticCategory()
{
    a = 0;
    b = 0;
    c = 0;
    d = 0;
    e = 0;

    f = 0;
    g = 0;
    h = 0;
    i = 0;
    j = 0;
}

SrsStatisticCategory::~SrsStatisticCategory()
{
}

SrsStatistic* SrsStatistic::_instance = NULL;

SrsStatistic::SrsStatistic()
{
    _server_id = srs_generate_id();
    
    clk = new SrsWallClock();
    kbps = new SrsKbps(clk);
    kbps->set_io(NULL, NULL);

    perf_iovs = new SrsStatisticCategory();
    perf_msgs = new SrsStatisticCategory();
    perf_sendmmsg = new SrsStatisticCategory();
    perf_gso = new SrsStatisticCategory();
}

SrsStatistic::~SrsStatistic()
{
    srs_freep(kbps);
    srs_freep(clk);
    
    if (true) {
        std::map<int64_t, SrsStatisticVhost*>::iterator it;
        for (it = vhosts.begin(); it != vhosts.end(); it++) {
            SrsStatisticVhost* vhost = it->second;
            srs_freep(vhost);
        }
    }
    if (true) {
        std::map<int64_t, SrsStatisticStream*>::iterator it;
        for (it = streams.begin(); it != streams.end(); it++) {
            SrsStatisticStream* stream = it->second;
            srs_freep(stream);
        }
    }
    if (true) {
        std::map<int, SrsStatisticClient*>::iterator it;
        for (it = clients.begin(); it != clients.end(); it++) {
            SrsStatisticClient* client = it->second;
            srs_freep(client);
        }
    }
    
    vhosts.clear();
    rvhosts.clear();
    streams.clear();
    rstreams.clear();

    srs_freep(perf_iovs);
    srs_freep(perf_msgs);
    srs_freep(perf_sendmmsg);
    srs_freep(perf_gso);
}

SrsStatistic* SrsStatistic::instance()
{
    if (_instance == NULL) {
        _instance = new SrsStatistic();
    }
    return _instance;
}

SrsStatisticVhost* SrsStatistic::find_vhost(int vid)
{
    std::map<int64_t, SrsStatisticVhost*>::iterator it;
    if ((it = vhosts.find(vid)) != vhosts.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticVhost* SrsStatistic::find_vhost(string name)
{
    if (rvhosts.empty()) {
        return NULL;
    }
    
    std::map<string, SrsStatisticVhost*>::iterator it;
    if ((it = rvhosts.find(name)) != rvhosts.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticStream* SrsStatistic::find_stream(int sid)
{
    std::map<int64_t, SrsStatisticStream*>::iterator it;
    if ((it = streams.find(sid)) != streams.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticClient* SrsStatistic::find_client(int cid)
{
    std::map<int, SrsStatisticClient*>::iterator it;
    if ((it = clients.find(cid)) != clients.end()) {
        return it->second;
    }
    return NULL;
}

srs_error_t SrsStatistic::on_video_info(SrsRequest* req, SrsVideoCodecId vcodec, SrsAvcProfile avc_profile, SrsAvcLevel avc_level, int width, int height)
{
    srs_error_t err = srs_success;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);
    
    stream->has_video = true;
    stream->vcodec = vcodec;
    stream->avc_profile = avc_profile;
    stream->avc_level = avc_level;
    
    stream->width = width;
    stream->height = height;
    
    return err;
}

srs_error_t SrsStatistic::on_audio_info(SrsRequest* req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    srs_error_t err = srs_success;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);
    
    stream->has_audio = true;
    stream->acodec = acodec;
    stream->asample_rate = asample_rate;
    stream->asound_type = asound_type;
    stream->aac_object = aac_object;
    
    return err;
}

srs_error_t SrsStatistic::on_video_frames(SrsRequest* req, int nb_frames)
{
    srs_error_t err = srs_success;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);
    
    stream->nb_frames += nb_frames;
    
    return err;
}

void SrsStatistic::on_stream_publish(SrsRequest* req, int cid)
{
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);
    
    stream->publish(cid);
}

void SrsStatistic::on_stream_close(SrsRequest* req)
{
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);
    stream->close();
    
    // TODO: FIXME: Should fix https://github.com/ossrs/srs/issues/803
    if (true) {
        std::map<int64_t, SrsStatisticStream*>::iterator it;
        if ((it=streams.find(stream->id)) != streams.end()) {
            streams.erase(it);
        }
    }
    
    // TODO: FIXME: Should fix https://github.com/ossrs/srs/issues/803
    if (true) {
        std::map<std::string, SrsStatisticStream*>::iterator it;
        if ((it=rstreams.find(stream->url)) != rstreams.end()) {
            rstreams.erase(it);
        }
    }
}

srs_error_t SrsStatistic::on_client(int id, SrsRequest* req, SrsConnection* conn, SrsRtmpConnType type)
{
    srs_error_t err = srs_success;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);
    
    // create client if not exists
    SrsStatisticClient* client = NULL;
    if (clients.find(id) == clients.end()) {
        client = new SrsStatisticClient();
        client->id = id;
        client->stream = stream;
        clients[id] = client;
    } else {
        client = clients[id];
    }
    
    // got client.
    client->conn = conn;
    client->req = req;
    client->type = type;
    stream->nb_clients++;
    vhost->nb_clients++;
    
    return err;
}

void SrsStatistic::on_disconnect(int id)
{
    std::map<int, SrsStatisticClient*>::iterator it;
    if ((it = clients.find(id)) == clients.end()) {
        return;
    }
    
    SrsStatisticClient* client = it->second;
    SrsStatisticStream* stream = client->stream;
    SrsStatisticVhost* vhost = stream->vhost;
    
    srs_freep(client);
    clients.erase(it);
    
    stream->nb_clients--;
    vhost->nb_clients--;
}

void SrsStatistic::kbps_add_delta(SrsConnection* conn)
{
    int id = conn->srs_id();
    if (clients.find(id) == clients.end()) {
        return;
    }
    
    SrsStatisticClient* client = clients[id];
    
    // resample the kbps to collect the delta.
    int64_t in, out;
    conn->remark(&in, &out);
    
    // add delta of connection to kbps.
    // for next sample() of server kbps can get the stat.
    kbps->add_delta(in, out);
    client->stream->kbps->add_delta(in, out);
    client->stream->vhost->kbps->add_delta(in, out);
}

SrsKbps* SrsStatistic::kbps_sample()
{
    kbps->sample();
    if (true) {
        std::map<int64_t, SrsStatisticVhost*>::iterator it;
        for (it = vhosts.begin(); it != vhosts.end(); it++) {
            SrsStatisticVhost* vhost = it->second;
            vhost->kbps->sample();
        }
    }
    if (true) {
        std::map<int64_t, SrsStatisticStream*>::iterator it;
        for (it = streams.begin(); it != streams.end(); it++) {
            SrsStatisticStream* stream = it->second;
            stream->kbps->sample();
        }
    }
    
    return kbps;
}

int64_t SrsStatistic::server_id()
{
    return _server_id;
}

srs_error_t SrsStatistic::dumps_vhosts(SrsJsonArray* arr)
{
    srs_error_t err = srs_success;
    
    std::map<int64_t, SrsStatisticVhost*>::iterator it;
    for (it = vhosts.begin(); it != vhosts.end(); it++) {
        SrsStatisticVhost* vhost = it->second;
        
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        
        if ((err = vhost->dumps(obj)) != srs_success) {
            return srs_error_wrap(err, "dump vhost");
        }
    }
    
    return err;
}

srs_error_t SrsStatistic::dumps_streams(SrsJsonArray* arr)
{
    srs_error_t err = srs_success;
    
    std::map<int64_t, SrsStatisticStream*>::iterator it;
    for (it = streams.begin(); it != streams.end(); it++) {
        SrsStatisticStream* stream = it->second;
        
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        
        if ((err = stream->dumps(obj)) != srs_success) {
            return srs_error_wrap(err, "dump stream");
        }
    }
    
    return err;
}

srs_error_t SrsStatistic::dumps_clients(SrsJsonArray* arr, int start, int count)
{
    srs_error_t err = srs_success;
    
    std::map<int, SrsStatisticClient*>::iterator it = clients.begin();
    for (int i = 0; i < start + count && it != clients.end(); it++, i++) {
        if (i < start) {
            continue;
        }
        
        SrsStatisticClient* client = it->second;
        
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        
        if ((err = client->dumps(obj)) != srs_success) {
            return srs_error_wrap(err, "dump client");
        }
    }
    
    return err;
}

void SrsStatistic::perf_mw_on_msgs(int nb_msgs, int bytes_msgs, int nb_iovs)
{
    // For perf msgs, the nb_msgs stat.
    //      a: =1
    //      b: <3
    //      c: <6
    //      d: <12
    //      e: <128
    //      f: <256
    //      g: <512
    //      h: <600
    //      i: <1000
    //      j: >=1000
    if (nb_msgs == 1) {
        perf_msgs->a++;
    } else if (nb_msgs < 3) {
        perf_msgs->b++;
    } else if (nb_msgs < 6) {
        perf_msgs->c++;
    } else if (nb_msgs < 12) {
        perf_msgs->d++;
    } else if (nb_msgs < 128) {
        perf_msgs->e++;
    } else if (nb_msgs < 256) {
        perf_msgs->f++;
    } else if (nb_msgs < 512) {
        perf_msgs->g++;
    } else if (nb_msgs < 600) {
        perf_msgs->h++;
    } else if (nb_msgs < 1000) {
        perf_msgs->i++;
    } else {
        perf_msgs->j++;
    }

    // For perf iovs, the nb_iovs stat.
    //      a: <=2
    //      b: <10
    //      c: <20
    //      d: <200
    //      e: <300
    //      f: <500
    //      g: <700
    //      h: <900
    //      i: <1024
    //      j: >=1024
    if (nb_iovs <= 2) {
        perf_iovs->a++;
    } else if (nb_iovs < 10) {
        perf_iovs->b++;
    } else if (nb_iovs < 20) {
        perf_iovs->c++;
    } else if (nb_iovs < 200) {
        perf_iovs->d++;
    } else if (nb_iovs < 300) {
        perf_iovs->e++;
    } else if (nb_iovs < 500) {
        perf_iovs->f++;
    } else if (nb_iovs < 700) {
        perf_iovs->g++;
    } else if (nb_iovs < 900) {
        perf_iovs->h++;
    } else if (nb_iovs < 1024) {
        perf_iovs->i++;
    } else {
        perf_iovs->j++;
    }
}

srs_error_t SrsStatistic::dumps_perf_writev(SrsJsonObject* obj)
{
    srs_error_t err = srs_success;

    if (true) {
        SrsJsonObject* p = SrsJsonAny::object();
        obj->set("msgs", p);

        // For perf msgs, the nb_msgs stat.
        //      a: =1
        //      b: <3
        //      c: <6
        //      d: <12
        //      e: <128
        //      f: <256
        //      g: <512
        //      h: <600
        //      i: <1000
        //      j: >=1000
        p->set("lt_2",      SrsJsonAny::integer(perf_msgs->a));
        p->set("lt_3",     SrsJsonAny::integer(perf_msgs->b));
        p->set("lt_6",    SrsJsonAny::integer(perf_msgs->c));
        p->set("lt_12",    SrsJsonAny::integer(perf_msgs->d));
        p->set("lt_128",    SrsJsonAny::integer(perf_msgs->e));
        p->set("lt_256",    SrsJsonAny::integer(perf_msgs->f));
        p->set("lt_512",    SrsJsonAny::integer(perf_msgs->g));
        p->set("lt_600",    SrsJsonAny::integer(perf_msgs->h));
        p->set("lt_1000",   SrsJsonAny::integer(perf_msgs->i));
        p->set("gt_1000",   SrsJsonAny::integer(perf_msgs->j));
    }

    if (true) {
        SrsJsonObject* p = SrsJsonAny::object();
        obj->set("iovs", p);

        // For perf iovs, the nb_iovs stat.
        //      a: <=2
        //      b: <10
        //      c: <20
        //      d: <200
        //      e: <300
        //      f: <500
        //      g: <700
        //      h: <900
        //      i: <1024
        //      j: >=1024
        p->set("lt_3",      SrsJsonAny::integer(perf_iovs->a));
        p->set("lt_10",     SrsJsonAny::integer(perf_iovs->b));
        p->set("lt_20",     SrsJsonAny::integer(perf_iovs->c));
        p->set("lt_200",    SrsJsonAny::integer(perf_iovs->d));
        p->set("lt_300",    SrsJsonAny::integer(perf_iovs->e));
        p->set("lt_500",    SrsJsonAny::integer(perf_iovs->f));
        p->set("lt_700",    SrsJsonAny::integer(perf_iovs->g));
        p->set("lt_900",    SrsJsonAny::integer(perf_iovs->h));
        p->set("lt_1024",   SrsJsonAny::integer(perf_iovs->i));
        p->set("gt_1024",   SrsJsonAny::integer(perf_iovs->j));
    }

    return err;
}

void SrsStatistic::perf_sendmmsg_on_packets(int nb_msgs)
{
    // For perf msgs, the nb_msgs stat.
    //      a: =1
    //      b: <10
    //      c: <100
    //      d: <200
    //      e: <300
    //      f: <400
    //      g: <500
    //      h: <600
    //      i: <1000
    //      j: >=1000
    if (nb_msgs == 1) {
        perf_sendmmsg->a++;
    } else if (nb_msgs < 10) {
        perf_sendmmsg->b++;
    } else if (nb_msgs < 100) {
        perf_sendmmsg->c++;
    } else if (nb_msgs < 200) {
        perf_sendmmsg->d++;
    } else if (nb_msgs < 300) {
        perf_sendmmsg->e++;
    } else if (nb_msgs < 400) {
        perf_sendmmsg->f++;
    } else if (nb_msgs < 500) {
        perf_sendmmsg->g++;
    } else if (nb_msgs < 600) {
        perf_sendmmsg->h++;
    } else if (nb_msgs < 1000) {
        perf_sendmmsg->i++;
    } else {
        perf_sendmmsg->j++;
    }
}

srs_error_t SrsStatistic::dumps_perf_sendmmsg(SrsJsonObject* obj)
{
    srs_error_t err = srs_success;

    if (true) {
        SrsJsonObject* p = SrsJsonAny::object();
        obj->set("msgs", p);

        // For perf msgs, the nb_msgs stat.
        //      a: =1
        //      b: <10
        //      c: <100
        //      d: <200
        //      e: <300
        //      f: <400
        //      g: <500
        //      h: <600
        //      i: <1000
        //      j: >=1000
        p->set("lt_2",      SrsJsonAny::integer(perf_sendmmsg->a));
        p->set("lt_10",     SrsJsonAny::integer(perf_sendmmsg->b));
        p->set("lt_100",    SrsJsonAny::integer(perf_sendmmsg->c));
        p->set("lt_200",    SrsJsonAny::integer(perf_sendmmsg->d));
        p->set("lt_300",    SrsJsonAny::integer(perf_sendmmsg->e));
        p->set("lt_400",    SrsJsonAny::integer(perf_sendmmsg->f));
        p->set("lt_500",    SrsJsonAny::integer(perf_sendmmsg->g));
        p->set("lt_600",    SrsJsonAny::integer(perf_sendmmsg->h));
        p->set("lt_1000",   SrsJsonAny::integer(perf_sendmmsg->i));
        p->set("gt_1000",   SrsJsonAny::integer(perf_sendmmsg->j));
    }

    return err;
}

void SrsStatistic::perf_gso_on_packets(int nb_msgs)
{
    // For perf msgs, the nb_msgs stat.
    //      a: =1
    //      b: <3
    //      c: <6
    //      d: <9
    //      e: <16
    //      f: <32
    //      g: <64
    //      h: <128
    //      i: <512
    //      j: >=512
    if (nb_msgs == 1) {
        perf_gso->a++;
    } else if (nb_msgs < 3) {
        perf_gso->b++;
    } else if (nb_msgs < 6) {
        perf_gso->c++;
    } else if (nb_msgs < 9) {
        perf_gso->d++;
    } else if (nb_msgs < 16) {
        perf_gso->e++;
    } else if (nb_msgs < 32) {
        perf_gso->f++;
    } else if (nb_msgs < 64) {
        perf_gso->g++;
    } else if (nb_msgs < 128) {
        perf_gso->h++;
    } else if (nb_msgs < 512) {
        perf_gso->i++;
    } else {
        perf_gso->j++;
    }
}

srs_error_t SrsStatistic::dumps_perf_gso(SrsJsonObject* obj)
{
    srs_error_t err = srs_success;

    if (true) {
        SrsJsonObject* p = SrsJsonAny::object();
        obj->set("msgs", p);

        // For perf msgs, the nb_msgs stat.
        //      a: =1
        //      b: <3
        //      c: <6
        //      d: <9
        //      e: <16
        //      f: <32
        //      g: <64
        //      h: <128
        //      i: <512
        //      j: >=512
        p->set("lt_2",      SrsJsonAny::integer(perf_gso->a));
        p->set("lt_3",     SrsJsonAny::integer(perf_gso->b));
        p->set("lt_6",    SrsJsonAny::integer(perf_gso->c));
        p->set("lt_9",    SrsJsonAny::integer(perf_gso->d));
        p->set("lt_16",    SrsJsonAny::integer(perf_gso->e));
        p->set("lt_32",    SrsJsonAny::integer(perf_gso->f));
        p->set("lt_64",    SrsJsonAny::integer(perf_gso->g));
        p->set("lt_128",    SrsJsonAny::integer(perf_gso->h));
        p->set("lt_512",   SrsJsonAny::integer(perf_gso->i));
        p->set("gt_512",   SrsJsonAny::integer(perf_gso->j));
    }

    return err;
}

SrsStatisticVhost* SrsStatistic::create_vhost(SrsRequest* req)
{
    SrsStatisticVhost* vhost = NULL;
    
    // create vhost if not exists.
    if (rvhosts.find(req->vhost) == rvhosts.end()) {
        vhost = new SrsStatisticVhost();
        vhost->vhost = req->vhost;
        rvhosts[req->vhost] = vhost;
        vhosts[vhost->id] = vhost;
        return vhost;
    }
    
    vhost = rvhosts[req->vhost];
    
    return vhost;
}

SrsStatisticStream* SrsStatistic::create_stream(SrsStatisticVhost* vhost, SrsRequest* req)
{
    std::string url = req->get_stream_url();
    
    SrsStatisticStream* stream = NULL;
    
    // create stream if not exists.
    if (rstreams.find(url) == rstreams.end()) {
        stream = new SrsStatisticStream();
        stream->vhost = vhost;
        stream->stream = req->stream;
        stream->app = req->app;
        stream->url = url;
        rstreams[url] = stream;
        streams[stream->id] = stream;
        return stream;
    }
    
    stream = rstreams[url];
    
    return stream;
}

