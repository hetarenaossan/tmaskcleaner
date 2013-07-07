#include <Windows.h>
#include <vector>
#pragma warning(disable: 4512 4244 4100)
#include "avisynth.h"
#pragma warning(default: 4512 4244 4100)

using namespace std;

typedef pair<int, int> Coordinates;

class TMaskCleaner : public GenericVideoFilter {
public:
    TMaskCleaner(PClip child, int length, int thresh, IScriptEnvironment*);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);

    ~TMaskCleaner() {
        delete [] buffer;
        if (lookup != nullptr) {
            delete[] lookup;
        }
    }
private:
    unsigned int m_length;
    unsigned int m_thresh;
    Coordinates* buffer;
    BYTE *lookup;
    int m_w;

    void ClearMask(BYTE *dst, const BYTE *src, int width, int height, int src_pitch, int dst_pitch);

    inline bool IsWhite(BYTE value) {
        return value >= m_thresh;
    }

    inline bool Visited(int x, int y) {
        unsigned int normal_pos = y * m_w + x;
        unsigned int byte_pos = normal_pos / 8;

        return lookup[byte_pos] & (1 << (normal_pos - byte_pos*8));
    }

    inline void Visit(int x, int y) {
        unsigned int normal_pos = y * m_w + x;
        unsigned int byte_pos = normal_pos / 8;

        lookup[byte_pos] |= (1 << (normal_pos - byte_pos*8));
    }
};

TMaskCleaner::TMaskCleaner(PClip child, int length, int thresh, IScriptEnvironment* env) :
    GenericVideoFilter(child),
    m_length(length),
    m_thresh(thresh),
    lookup(nullptr)
{
    if (!child->GetVideoInfo().IsYV12()) {
        env->ThrowError("Only YV12 and YV24 is supported!");
    }
    if (length <= 0 || thresh <= 0) {
        env->ThrowError("Invalid arguments!");
    }
    lookup = new BYTE[child->GetVideoInfo().height * child->GetVideoInfo().width / 8];
    buffer = new Coordinates[length];
    m_w = child->GetVideoInfo().width;
}

PVideoFrame TMaskCleaner::GetFrame(int n, IScriptEnvironment* env) {
    PVideoFrame src = child->GetFrame(n,env);
    PVideoFrame dst = env->NewVideoFrame(child->GetVideoInfo());

    memset(dst->GetWritePtr(PLANAR_Y), 0, dst->GetPitch(PLANAR_Y) * dst->GetHeight(PLANAR_Y));
    memset(lookup, 0, child->GetVideoInfo().height * child->GetVideoInfo().width / 8);

    ClearMask(dst->GetWritePtr(PLANAR_Y), src->GetReadPtr(PLANAR_Y), dst->GetRowSize(PLANAR_Y), dst->GetHeight(PLANAR_Y),src->GetPitch(PLANAR_Y), dst->GetPitch(PLANAR_Y));
    return dst;
}

void TMaskCleaner::ClearMask(BYTE *dst, const BYTE *src, int w, int h, int src_pitch, int dst_pitch) {
    vector<Coordinates> coordinates;
    int b;
    Coordinates current;
    for(int y = 0; y < h; ++y) {
        for(int x = 0; x < w; ++x) {
            if (Visited(x,y)) {
                continue;
            }
            Visit(x,y);
            if(!IsWhite(src[src_pitch * y + x])) {
                continue;
            }
            buffer[0]=make_pair(x,y);
            b=1;
            coordinates.clear();
            coordinates.emplace_back(x,y);
            while(!coordinates.empty()){
                current = coordinates.back();
                coordinates.pop_back();
                int x_min = current.first  == 0 ? 0 : current.first - 1;
                int x_max = current.first  == w - 1 ? w : current.first + 2;
                int y_min = current.second == 0 ? 0 : current.second - 1;
                int y_max = current.second == h - 1 ? h : current.second + 2;
                for (int j = y_min; j < y_max; ++j ) {
                    for (int i = x_min; i < x_max; ++i ) {
                        if (!Visited(i,j)){
                            Visit(i,j);
                            if(IsWhite(src[j * src_pitch + i])){
                                coordinates.emplace_back(i,j);
                                if(b<m_length){
                                    buffer[b++] = make_pair(i,j);
                                } else {
                                    dst[dst_pitch * j + i] = src[src_pitch * j + i];
                                }
                            }

                        }
                    }
                }
            }
            if(b>=m_length){
                for(int i = 0;i<m_length;i++){
                    dst[dst_pitch * buffer[i].second + buffer[i].first] = src[src_pitch * buffer[i].second + buffer[i].first];
                }
            }
        }
    }
}

AVSValue __cdecl Create_TMaskCleaner(AVSValue args, void*, IScriptEnvironment* env)
{
    enum { CLIP, LENGTH, THRESH};
    return new TMaskCleaner(args[CLIP].AsClip(), args[LENGTH].AsInt(5), args[THRESH].AsInt(235), env);
}

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit2(IScriptEnvironment* env) {
    env->AddFunction("TMaskCleaner", "c[length]i[thresh]i", Create_TMaskCleaner, 0);
    return "Why are you looking at this?";
}
