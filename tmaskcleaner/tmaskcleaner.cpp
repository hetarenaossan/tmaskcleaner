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
        delete [] mask;
        delete [] buffer;
    }
private:
    unsigned int m_length;
    unsigned int m_thresh;
    int* buffer;
    BYTE *mask;
    int m_w;

    void ClearMask(BYTE *dst, const BYTE *src, int width, int height, int src_pitch, int dst_pitch);

    inline bool IsWhite(BYTE value) {
        return value >= m_thresh;
    }

    inline bool Visited(int pos) {
        return mask[pos] != 1;
    }

    inline void Visit(int pos) {
        mask[pos] = 0;
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
    buffer = new int[length];
    mask = new BYTE[child->GetVideoInfo().height * child->GetVideoInfo().width];
    m_w = child->GetVideoInfo().width;
}

PVideoFrame TMaskCleaner::GetFrame(int n, IScriptEnvironment* env) {
    PVideoFrame src = child->GetFrame(n,env);
    PVideoFrame dst = env->NewVideoFrame(child->GetVideoInfo());

    memset(mask, 1, child->GetVideoInfo().height * child->GetVideoInfo().width);

    ClearMask(dst->GetWritePtr(PLANAR_Y), src->GetReadPtr(PLANAR_Y), dst->GetRowSize(PLANAR_Y), dst->GetHeight(PLANAR_Y),src->GetPitch(PLANAR_Y), dst->GetPitch(PLANAR_Y));
    return dst;
}

void TMaskCleaner::ClearMask(BYTE *dst, const BYTE *src, int w, int h, int src_pitch, int dst_pitch) {
    vector<Coordinates> coordinates;
    int b;
    Coordinates current;
    for(int y = 0; y < h; ++y) {
        for(int x = 0; x < w; ++x) {
            int pos = src_pitch * y + x;
            if (Visited(pos)) {
                continue;
            }
            Visit(pos);
            if(!IsWhite(src[pos])) {
                continue;
            }
            buffer[0]=pos;
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
                        pos = src_pitch * j + i;
                        if (!Visited(pos)){
                            Visit(pos);
                            if(IsWhite(src[pos])){
                                coordinates.emplace_back(i,j);
                                if(b<m_length){
                                    buffer[b++] = pos;
                                } else {
                                    mask[pos] = 0xFF;
                                }
                            }

                        }
                    }
                }
            }
            if(b>=m_length){
                for(int i = 0;i<m_length;i++){
                    mask[buffer[i]] = 0xFF;
                }
            }
        }
    }

    uint32_t* inp = (uint32_t*)src;
    uint32_t* m = (uint32_t*)mask;
    uint32_t* res = (uint32_t*)dst;
    int cnt = (w*h) >> 2;
    for (int i = 0 ; i < cnt ; i++ ){
        res[i] = inp[i] & m[i];
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
