#include <Windows.h>
#include <vector>
#include <list>
#include <memory>
#pragma warning(disable: 4512 4244 4100)
#include "avisynth.h"
#pragma warning(default: 4512 4244 4100)
#include <mutex>
#include <emmintrin.h>

typedef std::pair<int, int> Coordinates;

namespace {

    template <class T>
    class ArrayAccessor;

    template <class T>
    class Array {
        friend class ArrayAccessor<T>;
    private:
        mutable std::mutex m;
        bool l;
    public:
        int size;
        T* ptr;

        Array(int size_):
            size(size),
            l(false)
        {
            ptr = new T[size];
        }

        Array():
            ptr(nullptr),
            l(false)
        {};

        ~Array(){
            if(ptr!=nullptr) delete [] ptr;
        }

        Array(Array<T>&& a):
            ptr(a.ptr),
            l(a.l)
        {
            a.ptr = nullptr;
        }

        Array<T>& operator=(Array<T>&& a){
            ptr = a.ptr;
            a.ptr = nullptr;
            l = a.l;
            return *this;
        }

        bool locked() const {
            std::lock_guard<std::mutex> lock(m);
            return l;
        }
    };

    template <class T>
    class ArrayAccessor {
    private:
        Array<T> * array;
    public:
        T* ptr;

        ArrayAccessor(Array<T>* array_):
            array(array_),
            ptr(array_->ptr)
        {
            std::lock_guard<std::mutex> lock(array->m);
            array->l = true;
        }

        ArrayAccessor():
            array(nullptr),
            ptr(nullptr)
        {}

        ArrayAccessor(ArrayAccessor<T>&& a):
            array(a.array),
            ptr(a.ptr)
        {
            a.array = nullptr;
        }

        ArrayAccessor<T>& operator=(ArrayAccessor<T>&& a){
            array = a.array;
            ptr = a.ptr;
            a.array = nullptr;
            return *this;
        }

        ~ArrayAccessor() {
            if(array!=nullptr){
                std::lock_guard<std::mutex> lock(array->m);
                array->l = false;
            }
        }
    };

    template <class T>
    class DynamicBuffer {
    private:
        std::mutex m;
        int size;
        std::list<std::unique_ptr<Array<T>>> list;
    public:
        DynamicBuffer(int size_):
            size(size_)
        {};

        ArrayAccessor<T> GetBuffer(){
            std::lock_guard<std::mutex> lock(m);
            for(const auto it=list.begin();it!=list.end();it++){
                if(!(*it)->locked()){
                    ArrayAccessor<T> a((*it).get());
                    return a;
                }
            }
            list.emplace_back(new Array<T>(size));
            ArrayAccessor<T> a(list.back().get());
            return a;
        }
    };
}


class TMaskCleaner : public GenericVideoFilter {
public:
    TMaskCleaner(PClip child, int length, int thresh, IScriptEnvironment*);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);

    ~TMaskCleaner() {}
private:
    unsigned int m_length;
    unsigned int m_thresh;
    DynamicBuffer<int> buffer;
    DynamicBuffer<BYTE> mask;
    DynamicBuffer<Coordinates> coords;
    int m_w;
    int size;

    void ClearMask(BYTE *dst, const BYTE *src, int width, int height, int src_pitch, int dst_pitch);
};

TMaskCleaner::TMaskCleaner(PClip child, int length, int thresh, IScriptEnvironment* env) :
    GenericVideoFilter(child),
    m_length(length),
    m_thresh(thresh),
    buffer(length),
    mask(child->GetVideoInfo().height * (child->GetVideoInfo().width +16)),
    coords(child->GetVideoInfo().height * child->GetVideoInfo().width)
{
    if (!child->GetVideoInfo().IsYV12()) {
        env->ThrowError("Only YV12 and YV24 is supported!");
    }
    if (length <= 0 || thresh <= 0) {
        env->ThrowError("Invalid arguments!");
    }
    int CPUInfo[4]; //eax, ebx, ecx, edx
    __cpuid(CPUInfo, 1);

    if (!(CPUInfo[2] & 0x00000200)) {
        env->ThrowError("Sorry, SSSE3 is required");
    }
    m_w = child->GetVideoInfo().width;
}

PVideoFrame TMaskCleaner::GetFrame(int n, IScriptEnvironment* env) {
    PVideoFrame src = child->GetFrame(n,env);
    PVideoFrame dst = env->NewVideoFrame(child->GetVideoInfo());

    ClearMask(dst->GetWritePtr(PLANAR_Y), src->GetReadPtr(PLANAR_Y), dst->GetRowSize(PLANAR_Y), dst->GetHeight(PLANAR_Y),src->GetPitch(PLANAR_Y), dst->GetPitch(PLANAR_Y));
    return dst;
}

void TMaskCleaner::ClearMask(BYTE *dst, const BYTE *src, int w, int h, int src_pitch, int dst_pitch) {
    ArrayAccessor<int> buffer_accessor = buffer.GetBuffer();
    ArrayAccessor<BYTE> mask_accessor = mask.GetBuffer();
    ArrayAccessor<Coordinates> coords_accessor = coords.GetBuffer();
    int* buf = buffer_accessor.ptr;
    BYTE* m = mask_accessor.ptr;
    Coordinates* coordinates = coords_accessor.ptr;
    memset(m,1,h*src_pitch);
    int b,cs;
    Coordinates current;
    for(int y = 0; y < h; ++y) {
        for(int x = 0; x < w; ++x) {
            int pos = src_pitch * y + x;
            if (m[pos]!=1) {
                continue;
            }
            m[pos]=0;
            if(src[pos]<=m_thresh) {
                continue;
            }
            buf[0]=pos;
            b=1;
            coordinates[0] = Coordinates(x,y);
            cs = 1;
            while(cs>0){
                current = coordinates[--cs];
                int x_min = current.first  == 0 ? 0 : current.first - 1;
                int x_max = current.first  == w - 1 ? w : current.first + 2;
                int y_min = current.second == 0 ? 0 : current.second - 1;
                int y_max = current.second == h - 1 ? h : current.second + 2;
                for (int j = y_min; j < y_max; ++j ) {
                    for (int i = x_min; i < x_max; ++i ) {
                        pos = src_pitch * j + i;
                        if (m[pos]==1){
                            m[pos]=0;
                            if(src[pos]>m_thresh){
                                coordinates[cs++] = Coordinates(i,j);
                                if(b<m_length){
                                    buf[b++] = pos;
                                } else {
                                    m[pos] = 0xFF;
                                }
                            }

                        }
                    }
                }
            }
            if(b>=m_length){
                for(int i = 0;i<m_length;i++){
                    m[buf[i]] = 0xFF;
                }
            }
        }
    }

    int m16 = w / 16;
    int mw = m16*16;
    int sov = src_pitch - w;
    int dov = dst_pitch - w;
    for(int y = 0,sp =0 ,dp =0; y < h; y++){
        for(int x=0; x < m16; x++){
            __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src+sp));
            __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(m+sp));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst+dp), _mm_and_si128(a,b));
            sp+=16;
            dp+=16;
        }
        sp+=sov;
        dp+=dov;
    }
    if(w>mw){
        for(int y=0,sp=0,dp=0;y<h;y++){
            sp += mw;
            dp += mw;
            for(int x=mw;x<w;x++,sp++,dp++){
                dst[dp] = src[sp] & m[sp];
            }
            sp+= sov;
            dp+= dov;
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
