#include "CLSMISM.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <string>
#include <iostream>
#include <limits>
#include <memory>

// Include pocketfft single-header from thirdparty
#include "pocketfft/pocketfft_hdronly.h"

#ifndef M_PI
#define M_PI 3.141592653589793238462643383279502884L
#endif



namespace {

static void normalized_gaussian(const std::vector<double>& radial2, double sigma, std::vector<double>& out) {
    const size_t n = radial2.size();
    out.resize(n);
    if (sigma <= 1e-8) {
        double inv = 1.0 / static_cast<double>(n);
        std::fill(out.begin(), out.end(), inv);
        return;
    }
    double denom = 2.0 * sigma * sigma;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double val = std::exp(-radial2[i] / denom);
        out[i] = val;
        sum += val;
    }
    if (sum <= std::numeric_limits<double>::min()) {
        double inv = 1.0 / static_cast<double>(n);
        std::fill(out.begin(), out.end(), inv);
    } else {
        double inv_sum = 1.0 / sum;
        for (double& v : out) v *= inv_sum;
    }
}

// Simple 2D image
struct Image {
    size_t W{0}, H{0};
    std::vector<double> data; // row-major

    Image() = default;
    Image(size_t w, size_t h, double v=0.0): W(w), H(h), data(w*h, v) {}

    inline double& operator()(size_t y, size_t x) { return data[y*W + x]; }
    inline const double& operator()(size_t y, size_t x) const { return data[y*W + x]; }
};

// Complex image helpers used throughout this file
using cpx = std::complex<double>;
struct CImage {
    size_t W{0}, H{0};
    std::vector<cpx> data; // row-major

    CImage() = default;
    CImage(size_t w, size_t h, cpx v=cpx(0.0,0.0)) : W(w), H(h), data(w*h, v) {}

    inline cpx& operator()(size_t y, size_t x) { return data[y*W + x]; }
    inline const cpx& operator()(size_t y, size_t x) const { return data[y*W + x]; }
};

// ---------- pocketfft helpers (2D) ----------
namespace fft2d {
    // real->complex forward 2D FFT
    static void rfft2(const Image& in, CImage& out) {
        if (out.W != in.W || out.H != in.H) out = CImage(in.W, in.H);
        using pocketfft::shape_t; using pocketfft::stride_t;
        shape_t shape; shape.push_back(in.H); shape.push_back(in.W);
        stride_t s_in(2); s_in[0] = (ptrdiff_t)(in.W*sizeof(double)); s_in[1] = (ptrdiff_t)sizeof(double);
        stride_t s_out(2); s_out[0] = (ptrdiff_t)(out.W*sizeof(cpx));   s_out[1] = (ptrdiff_t)sizeof(cpx);
        shape_t axes; axes.push_back(0); axes.push_back(1);
        pocketfft::r2c(shape, s_in, s_out, axes, false,
                       in.data.data(), (cpx*)out.data.data(), 1.0);
    }

    // complex->real inverse 2D FFT (full spectrum expected)
    static void irfft2(const CImage& in, Image& out) {
        if (out.W != in.W || out.H != in.H) out = Image(in.W, in.H);
        using pocketfft::shape_t; using pocketfft::stride_t;
        shape_t shape; shape.push_back(out.H); shape.push_back(out.W);
        stride_t s_in(2);  s_in[0]  = (ptrdiff_t)(in.W*sizeof(cpx));    s_in[1]  = (ptrdiff_t)sizeof(cpx);
        stride_t s_out(2); s_out[0] = (ptrdiff_t)(out.W*sizeof(double)); s_out[1] = (ptrdiff_t)sizeof(double);
        shape_t axes; axes.push_back(0); axes.push_back(1);
        pocketfft::c2r(shape, s_in, s_out, axes, true,
                       (const cpx*)in.data.data(), out.data.data(), 1.0);
        // pocketfft::c2r with last arg 'true' applies normalization 1/N
    }

    // complex->complex forward or inverse
    static void cfft2(const CImage& in, CImage& out, bool inverse=false) {
        if (out.W != in.W || out.H != in.H) out = CImage(in.W, in.H);
        using pocketfft::shape_t; using pocketfft::stride_t;
        shape_t shape; shape.push_back(in.H); shape.push_back(in.W);
        stride_t s_in(2);  s_in[0]  = (ptrdiff_t)(in.W*sizeof(cpx)); s_in[1]  = (ptrdiff_t)sizeof(cpx);
        stride_t s_out(2); s_out[0] = (ptrdiff_t)(out.W*sizeof(cpx)); s_out[1] = (ptrdiff_t)sizeof(cpx);
        shape_t axes; axes.push_back(0); axes.push_back(1);
        pocketfft::c2c(shape, s_in, s_out, axes, !inverse, in.data.data(), out.data.data(), 1.0);
        // For inverse=true, pocketfft normalizes by 1 (not 1/N). We'll handle scaling explicitly if needed.
    }
}

// ---------- Fourier shift (subpixel) ----------
static void fourier_shift_inplace(CImage& F, double dx, double dy) {
    // Multiply spectrum by exp(-i 2π (u*dx/Nx + v*dy/Ny))
    const size_t W = F.W, H = F.H;
    const double two_pi = 2.0 * M_PI;
    for (size_t v=0; v<H; ++v) {
        double ky = (v<=H/2) ? (double)v : (double)v - (double)H; // wrap to neg freqs
        for (size_t u=0; u<W; ++u) {
            double kx = (u<=W/2) ? (double)u : (double)u - (double)W;
            double phase = -two_pi * (kx*dx / (double)W + ky*dy / (double)H);
            cpx ph(std::cos(phase), std::sin(phase));
            F(v,u) *= ph;
        }
    }
}

// Shift an image by (dx,dy) via FFT, return shifted
static Image subpixel_shift(const Image& in, double dx, double dy) {
    CImage F(in.W, in.H);
    fft2d::rfft2(in, F);
    fourier_shift_inplace(F, dx, dy);
    Image out(in.W, in.H);
    fft2d::irfft2(F, out);
    return out;
}

// ---------- Phase correlation to estimate x,y-shift between two images ----------
static std::pair<double,double> phase_correlation_shift(const Image& a, const Image& b) {
    if (a.W!=b.W || a.H!=b.H) throw std::runtime_error("phase_correlation_shift: size mismatch");
    size_t W=a.W, H=a.H;

    // FFT(a), FFT(b)
    CImage Fa, Fb, Ga, Gb;
    fft2d::rfft2(a, Fa);
    fft2d::rfft2(b, Fb);

    // Cross power spectrum CPS = Fa * conj(Fb) / |Fa*conj(Fb)| (avoid div by 0)
    CImage CPS(W,H);
    for (size_t i=0;i<W*H;++i) {
        cpx num = Fa.data[i] * std::conj(Fb.data[i]);
        double mag = std::abs(num);
        CPS.data[i] = (mag>1e-12) ? (num / mag) : cpx(0.0,0.0);
    }

    // Inverse FFT -> correlation surface
    Image corr;
    fft2d::irfft2(CPS, corr);

    // Find peak (wrap-aware): location gives shift (dy, dx)
    size_t peakx=0, peaky=0;
    double peak = -std::numeric_limits<double>::infinity();
    for (size_t y=0;y<H;++y) {
        for (size_t x=0;x<W;++x) {
            double v = corr(y,x);
            if (v>peak) { peak=v; peakx=x; peaky=y; }
        }
    }
    // Convert peak index to signed shift
    double dx = (peakx<=W/2) ? (double)peakx : (double)peakx - (double)W;
    double dy = (peaky<=H/2) ? (double)peaky : (double)peaky - (double)H;
    return {dx, dy};
}

// ---------- Wiener deconvolution in Fourier domain ----------
static Image wiener_deconvolution(const Image& img, const Image& psf, double K) {
    if (img.W!=psf.W || img.H!=psf.H) throw std::runtime_error("wiener_deconvolution: size mismatch");
    size_t W=img.W, H=img.H;

    CImage Fimg, Fpsf, Fout(W,H);
    fft2d::rfft2(img, Fimg);
    fft2d::rfft2(psf, Fpsf);

    for (size_t i=0;i<W*H;++i) {
        cpx Hk = Fpsf.data[i];
        double H2 = std::norm(Hk); // |H|^2
        cpx G = Fimg.data[i];
        cpx denom = cpx(H2 + K, 0.0);
        cpx Hw = (H2>0.0 || K>0.0) ? (std::conj(Hk)/denom) : cpx(0.0,0.0);
        Fout.data[i] = Hw * G;
    }
    Image out;
    fft2d::irfft2(Fout, out);
    return out;
}

// Simple utilities ----------
static Image sum_images(const std::vector<Image>& imgs) {
    if (imgs.empty()) return Image();
    Image acc(imgs[0].W, imgs[0].H, 0.0);
    for (const auto& im: imgs) {
        if (im.W!=acc.W || im.H!=acc.H) throw std::runtime_error("sum_images: size mismatch");
        for (size_t i=0;i<acc.data.size();++i) acc.data[i]+=im.data[i];
    }
    return acc;
}

static double sum_image(const Image& img) {
    double s = 0.0;
    for (double v : img.data) s += v;
    return s;
}

static void normalize_image(Image& img) {
    double s = sum_image(img);
    if (s > 0.0) {
        for (double& v : img.data) v /= s;
    }
}

static Image flip_image(const Image& in) {
    Image out(in.W, in.H, 0.0);
    for (size_t y = 0; y < in.H; ++y) {
        for (size_t x = 0; x < in.W; ++x) {
            out(in.H - 1 - y, in.W - 1 - x) = in(y, x);
        }
    }
    return out;
}

static void clamp_non_negative(Image& img, double eps = 0.0) {
    for (double& v : img.data) {
        if (v < eps) v = eps;
    }
}

static void zero_cimage(CImage& img) {
    std::fill(img.data.begin(), img.data.end(), cpx(0.0, 0.0));
}

static size_t next_power_of_two(size_t value) {
    if (value <= 1) return 1;
    size_t p = 1;
    while (p < value) {
        p <<= 1;
        if (p == 0) return value; // overflow guard
    }
    return p;
}

// 3D volume helper (z, y, x)
struct Volume {
    size_t Z{0}, Y{0}, X{0};
    std::vector<double> data;

    Volume() = default;
    Volume(size_t z, size_t y, size_t x, double v=0.0): Z(z), Y(y), X(x), data(z*y*x, v) {}

    inline double& operator()(size_t z, size_t y, size_t x) {
        return data[(z*Y + y)*X + x];
    }
    inline const double& operator()(size_t z, size_t y, size_t x) const {
        return data[(z*Y + y)*X + x];
    }
};

static Volume extract_data_volume(const std::vector<Image>& det_imgs) {
    if (det_imgs.empty()) return Volume();
    size_t D = det_imgs.size();
    size_t H = det_imgs[0].H;
    size_t W = det_imgs[0].W;
    Volume data(D, H, W, 0.0);
    for (size_t d = 0; d < D; ++d) {
        for (size_t y = 0; y < H; ++y)
            for (size_t x = 0; x < W; ++x)
                data(d, y, x) = det_imgs[d](y, x);
    }
    return data;
}

static Image make_gaussian_psf(size_t W, size_t H, double sigma_frac) {
    Image img(W, H, 0.0);
    double minwh = (double)std::min(W, H);
    double sigma = std::max(1e-6, sigma_frac * 0.5 * minwh);
    double s2 = 2.0 * sigma * sigma;
    double cx = (W - 1) * 0.5;
    double cy = (H - 1) * 0.5;
    double sum = 0.0;
    for (size_t y = 0; y < H; ++y) {
        for (size_t x = 0; x < W; ++x) {
            double dx = (double)x - cx;
            double dy = (double)y - cy;
            double v = std::exp(-(dx*dx + dy*dy) / s2);
            img(y, x) = v;
            sum += v;
        }
    }
    if (sum > 0) {
        for (size_t y = 0; y < H; ++y)
            for (size_t x = 0; x < W; ++x)
                img(y, x) /= sum;
    }
    return img;
}

static Volume build_psf_volume(const std::vector<Image>& det_imgs, size_t nz) {
    if (det_imgs.empty()) return Volume();
    size_t H = det_imgs[0].H;
    size_t W = det_imgs[0].W;
    Volume psf(nz, H, W, 0.0);
    const double base_sigma_frac = 0.015;
    for (size_t z = 0; z < nz; ++z) {
        double sigma_frac = base_sigma_frac * (1.0 + 0.05 * (double)z);
        Image base = make_gaussian_psf(W, H, sigma_frac);
        for (size_t y = 0; y < H; ++y)
            for (size_t x = 0; x < W; ++x)
                psf(z, y, x) = base(y, x);
    }
    return psf;
}

static Volume flip_volume(const Volume& vol) {
    Volume out(vol.Z, vol.Y, vol.X, 0.0);
    for (size_t z = 0; z < vol.Z; ++z)
        for (size_t y = 0; y < vol.Y; ++y)
            for (size_t x = 0; x < vol.X; ++x)
                out(z, vol.Y - 1 - y, vol.X - 1 - x) = vol(z, y, x);
    return out;
}

static void fill_padded_slice(const Volume& vol, size_t z, Image& dest, bool wrap_center) {
    if (dest.W < vol.X || dest.H < vol.Y) throw std::runtime_error("fill_padded_slice: destination too small");
    size_t shiftY = wrap_center ? vol.Y / 2 : 0;
    size_t shiftX = wrap_center ? vol.X / 2 : 0;
    for (size_t y = 0; y < vol.Y; ++y) {
        for (size_t x = 0; x < vol.X; ++x) {
            size_t yy = wrap_center ? (y + dest.H - shiftY) % dest.H : y;
            size_t xx = wrap_center ? (x + dest.W - shiftX) % dest.W : x;
            dest(yy, xx) = vol(z, y, x);
        }
    }
}

static void prepare_fft(const Volume& src, std::vector<CImage>& dst_fft, size_t padW, size_t padH, bool center_psf) {
    dst_fft.clear();
    dst_fft.reserve(src.Z);
    for (size_t z = 0; z < src.Z; ++z) {
        Image slice(padW, padH, 0.0);
        fill_padded_slice(src, z, slice, center_psf);
        CImage fft_slice(padW, padH);
        fft2d::rfft2(slice, fft_slice);
        dst_fft.push_back(std::move(fft_slice));
    }
}

static Volume amd_update_fft(const Volume& data, const Volume& obj, const std::vector<CImage>& psf_fft, const std::vector<CImage>& psf_m_fft, double eps, size_t padW, size_t padH) {
    Volume obj_new(obj.Z, obj.Y, obj.X, 0.0);
    for (size_t z = 0; z < obj.Z; ++z) {
        Image obj_slice(padW, padH, 0.0);
        fill_padded_slice(obj, z, obj_slice, false);
        CImage obj_fft(padW, padH);
        fft2d::rfft2(obj_slice, obj_fft);

        Image data_est(padW, padH, 0.0);
        for (size_t ch = 0; ch < data.Z; ++ch) {
            if (ch >= psf_fft.size()) break;
            CImage mult = psf_fft[ch];
            for (size_t idx = 0; idx < mult.data.size(); ++idx)
                mult.data[idx] *= obj_fft.data[idx];
            Image inv(padW, padH);
            fft2d::irfft2(mult, inv);
            for (size_t y = 0; y < obj.Y; ++y)
                for (size_t x = 0; x < obj.X; ++x)
                    data_est(y, x) += inv(y, x);
        }

        Image ratio(padW, padH, 0.0);
        for (size_t y = 0; y < obj.Y; ++y)
            for (size_t x = 0; x < obj.X; ++x)
                ratio(y, x) = (data_est(y, x) < eps) ? 0.0 : data(z, y, x) / data_est(y, x);
        CImage ratio_fft(padW, padH);
        fft2d::rfft2(ratio, ratio_fft);

        Image update(padW, padH, 0.0);
        for (size_t ch = 0; ch < data.Z; ++ch) {
            if (ch >= psf_m_fft.size()) break;
            CImage mult = psf_m_fft[ch];
            for (size_t idx = 0; idx < mult.data.size(); ++idx)
                mult.data[idx] *= ratio_fft.data[idx];
            Image inv(padW, padH);
            fft2d::irfft2(mult, inv);
            for (size_t y = 0; y < obj.Y; ++y)
                for (size_t x = 0; x < obj.X; ++x)
                    update(y, x) += inv(y, x);
        }

        for (size_t y = 0; y < obj.Y; ++y)
            for (size_t x = 0; x < obj.X; ++x)
                obj_new(z, y, x) = obj(z, y, x) * update(y, x);
    }
    return obj_new;
}

static std::pair<double, double> compute_flux(const Volume& vol, size_t focus_plane) {
    double focus_sum = 0.0;
    double total = 0.0;
    for (size_t z = 0; z < vol.Z; ++z) {
        for (size_t y = 0; y < vol.Y; ++y)
            for (size_t x = 0; x < vol.X; ++x) {
                double v = vol(z, y, x);
                total += v;
                if (z == focus_plane) focus_sum += v;
            }
    }
    return {focus_sum, total - focus_sum};
}

static bool amd_should_stop(const Volume& prev, const Volume& curr, size_t focus_plane, double tot_flux, double threshold, bool auto_stop) {
    if (!auto_stop) return false;
    auto [prev_focus, prev_bg] = compute_flux(prev, focus_plane);
    auto [curr_focus, curr_bg] = compute_flux(curr, focus_plane);
    double diff_focus = (curr_focus - prev_focus) / (tot_flux + 1e-12);
    double diff_bg = (curr_bg - prev_bg) / (tot_flux + 1e-12);
    return std::abs(diff_focus) < threshold && std::abs(diff_bg) < threshold;
}

// --- helpers: clamp, guided 3x3 smoothing, and optional box blur ---
static inline double clamp01(double v){ return (v<0.0)?0.0:(v>1.0?1.0:v); }

static void guided_smooth_B(Image& B, const Image& guide_sum, int iters=1) {
    // Edge-aware 3x3 smoothing: neighbors weighted by similarity in guide_sum
    const size_t W=B.W, H=B.H;
    Image tmp(W,H,0.0);
    const double eps = 1e-12;
    // Scale for intensity similarity (set relative to robust range)
    double gmin=*std::min_element(guide_sum.data.begin(), guide_sum.data.end());
    double gmax=*std::max_element(guide_sum.data.begin(), guide_sum.data.end());
    double s = (gmax>gmin)? 0.1*(gmax-gmin) : 1.0; // 10% of dynamic range
    if (s<=0.0) s=1.0;
    for (int it=0; it<iters; ++it) {
        std::fill(tmp.data.begin(), tmp.data.end(), 0.0);
        for (size_t y=0; y<H; ++y) {
            for (size_t x=0; x<W; ++x) {
                double wsum=0.0, acc=0.0;
                double g0 = guide_sum(y,x);
                for (int dy=-1; dy<=1; ++dy) {
                    size_t yy = (size_t)std::clamp<int>((int)y+dy, 0, (int)H-1);
                    for (int dx=-1; dx<=1; ++dx) {
                        size_t xx = (size_t)std::clamp<int>((int)x+dx, 0, (int)W-1);
                        double g1 = guide_sum(yy,xx);
                        double w = 1.0 / (1.0 + std::abs(g1 - g0)/(s+eps)); // edge-aware
                        acc  += w * B(yy,xx);
                        wsum += w;
                    }
                }
                tmp(y,x) = (wsum>eps)? acc/wsum : B(y,x);
            }
        }
        B.data.swap(tmp.data);
    }
}

static void box_blur_inplace(Image& im, int radius, int iters=1) {
    if (radius<=0 || iters<=0) return;
    const size_t W=im.W, H=im.H;
    Image tmp(W,H,0.0);
    for (int it=0; it<iters; ++it) {
        // horizontal
        for (size_t y=0; y<H; ++y) {
            double run=0.0;
            int r=radius;
            int wlen = 2*r+1;
            for (int i=-r; i<=r; ++i) {
                size_t xx = (size_t)std::clamp(i, 0, (int)W-1);
                run += im(y,xx);
            }
            for (size_t x=0; x<W; ++x) {
                tmp(y,x) = run / (double)wlen;
                int xout = (int)x - r;
                int xin  = (int)x + r + 1;
                if (xout>=0) run -= im(y,(size_t)xout);
                else         run -= im(y,0);
                if (xin<(int)W) run += im(y,(size_t)xin);
                else            run += im(y,W-1);
            }
        }
        // vertical
        for (size_t x=0; x<W; ++x) {
            double run=0.0;
            int r=radius;
            int hlen = 2*r+1;
            for (int i=-r; i<=r; ++i) {
                size_t yy = (size_t)std::clamp(i, 0, (int)H-1);
                run += tmp(yy,x);
            }
            for (size_t y=0; y<H; ++y) {
                im(y,x) = run / (double)hlen;
                int yout = (int)y - r;
                int yin  = (int)y + r + 1;
                if (yout>=0) run -= tmp((size_t)yout,x);
                else         run -= tmp(0,x);
                if (yin<(int)H) run += tmp((size_t)yin,x);
                else            run += tmp(H-1,x);
            }
        }
    }
}


static std::vector<Image> detector_images_from_array(const double* data, size_t dim0, size_t dim1, size_t dim2, bool channels_last) {
    if (!data) throw std::runtime_error("CLSMISM: null input array");

    size_t D = 0, H = 0, W = 0;
    if (channels_last) {
        H = dim0;
        W = dim1;
        D = dim2;
    } else {
        D = dim0;
        H = dim1;
        W = dim2;
    }

    if (D == 0 || H == 0 || W == 0) {
        throw std::runtime_error("CLSMISM: invalid array dimensions (zero sized)");
    }

    std::vector<Image> det_imgs;
    det_imgs.reserve(D);
    for (size_t d = 0; d < D; ++d) {
        det_imgs.emplace_back(W, H, 0.0);
    }

    if (channels_last) {
        for (size_t y = 0; y < H; ++y) {
            for (size_t x = 0; x < W; ++x) {
                const size_t base = ((size_t)y * W + x) * D;
                for (size_t d = 0; d < D; ++d) {
                    det_imgs[d](y, x) = data[base + d];
                }
            }
        }
    } else {
        const size_t plane = H * W;
        for (size_t d = 0; d < D; ++d) {
            const double* src = data + d * plane;
            Image& im = det_imgs[d];
            for (size_t y = 0; y < H; ++y) {
                for (size_t x = 0; x < W; ++x) {
                    im(y, x) = src[y * W + x];
                }
            }
        }
    }

    return det_imgs;
}

static void ensure_geometry(const std::vector<Image>& det_imgs) {
    if (det_imgs.empty()) {
        throw std::runtime_error("CLSMISM: no detector images available");
    }
    const size_t refW = det_imgs[0].W;
    const size_t refH = det_imgs[0].H;
    for (const auto& im : det_imgs) {
        if (im.W != refW || im.H != refH) {
            throw std::runtime_error("CLSMISM: detector images have inconsistent shapes");
        }
    }
}

static std::vector<std::pair<double,double>> estimate_detector_shifts_ref(const std::vector<Image>& det_imgs, size_t ref_idx);

static void apr_reconstruction_core(
    std::vector<Image>& det_imgs,
    double** output, int* dim1, int* dim2, int* dim3,
    int usf, int ref_idx, double filter_sigma,
    int nz)
{
    (void)usf;
    (void)filter_sigma;

    ensure_geometry(det_imgs);

    const size_t W = det_imgs[0].W;
    const size_t H = det_imgs[0].H;
    const size_t D = det_imgs.size();

    size_t ref = (ref_idx >= 0 && static_cast<size_t>(ref_idx) < D) ? static_cast<size_t>(ref_idx) : 0;
    std::vector<std::pair<double,double>> shifts = estimate_detector_shifts_ref(det_imgs, ref);

    std::vector<Image> filtered_det_imgs = det_imgs;

    Image sum_img(W, H, 0.0);
    for (size_t i = 0; i < D; ++i) {
        const auto [dx, dy] = shifts[i];
        Image shifted = (std::abs(dx) > 1e-9 || std::abs(dy) > 1e-9) ? subpixel_shift(filtered_det_imgs[i], dx, dy) : filtered_det_imgs[i];
        for (size_t k = 0; k < shifted.data.size(); ++k) {
            sum_img.data[k] += shifted.data[k];
        }
    }

    if (nz < 1) nz = 1;
    *dim1 = nz;
    *dim2 = static_cast<int>(H);
    *dim3 = static_cast<int>(W);
    const size_t plane = H * W;
    const size_t N = static_cast<size_t>(nz) * plane;
    double* buf = static_cast<double*>(std::malloc(N * sizeof(double)));
    if (!buf) throw std::bad_alloc();
    for (int iz = 0; iz < nz; ++iz) {
        std::copy(sum_img.data.begin(), sum_img.data.end(), buf + static_cast<size_t>(iz) * plane);
    }
    *output = buf;
}

static void focus_ism_core(
    std::vector<Image>& det_imgs,
    double sigma_bound, double threshold, int calibration_size,
    bool parallelize,
    int nz,
    double** output, int* dim1, int* dim2, int* dim3,
    const double* detector_coords, int detector_coords_len)
{
    (void)parallelize;
    (void)threshold; // (still unused here; keep for API compatibility)

    ensure_geometry(det_imgs);
    const size_t W = det_imgs[0].W;
    const size_t H = det_imgs[0].H;

    // ----- Tunables -----
    const double eps_norm        = 1e-12;
    const double lambda0         = 0.25;   // ridge prior for B
    const double sigma_min_abs   = 0.35;   // minimal PSF width in detector units
    const double sigma_up_factor = 6.0;    // how much broader the background PSF can be
    const int coarse_steps       = 24;
    const int fine_steps         = 16;
    const bool pre_blur_channels = false;  // optional per-channel 3x3 blur
    const int pre_blur_radius    = 1;
    const int B_smooth_iters     = 1;

    // ----- Determine active detectors -----
    size_t active_det = det_imgs.size();
    if (detector_coords && detector_coords_len > 1) {
        size_t provided_pairs = static_cast<size_t>(detector_coords_len / 2);
        active_det = std::min(active_det, provided_pairs);
    }
    det_imgs.resize(active_det);

    // ----- Build detector positions -----
    std::vector<std::pair<double,double>> detector_positions(active_det, {0.0, 0.0});

    if (detector_coords && detector_coords_len >= 2 * static_cast<int>(active_det)) {
        // Use externally provided coordinates (must match image order)
        for (size_t i = 0; i < active_det; ++i) {
            detector_positions[i].first  = detector_coords[2*i + 0];
            detector_positions[i].second = detector_coords[2*i + 1];
        }
    } else {
        // No coords: derive order-agnostic coords from measured shifts
        // 1) pick a robust reference (brightest; fallback=0)
        size_t ref_idx = 0;
        {
            double best_sum = -std::numeric_limits<double>::infinity();
            for (size_t i = 0; i < active_det; ++i) {
                double s = sum_image(det_imgs[i]);
                if (s > best_sum) { best_sum = s; ref_idx = i; }
            }
            if (!(best_sum > 0.0)) ref_idx = 0;
        }
        // 2) estimate shifts relative to reference
        std::vector<std::pair<double,double>> shifts = estimate_detector_shifts_ref(det_imgs, ref_idx);
        // 3) use (dx,dy) as coordinates; recenter and scale
        for (size_t i = 0; i < active_det; ++i) detector_positions[i] = shifts[i];
        double cx = 0.0, cy = 0.0;
        for (auto &p : detector_positions) { cx += p.first; cy += p.second; }
        cx /= (double)active_det; cy /= (double)active_det;
        for (auto &p : detector_positions) { p.first -= cx; p.second -= cy; }
        std::vector<double> rs; rs.reserve(active_det);
        for (auto &p : detector_positions) rs.push_back(std::hypot(p.first, p.second));
        if (!rs.empty()) {
            auto mid = rs.begin() + rs.size()/2;
            std::nth_element(rs.begin(), mid, rs.end());
            double med = *mid;
            if (med > 1e-9) {
                double s = 1.0 / med;
                for (auto &p : detector_positions) { p.first *= s; p.second *= s; }
            }
        }
    }

    // Precompute radial^2 from detector positions
    std::vector<double> radial2(active_det, 0.0);
    for (size_t i = 0; i < active_det; ++i) {
        double x = detector_positions[i].first;
        double y = detector_positions[i].second;
        radial2[i] = x*x + y*y;
    }

    // ----- Choose reference and estimate shifts for APR (visualization only) -----
    size_t ref_idx = 0;
    if (active_det > 0) {
        double best_sum = -std::numeric_limits<double>::infinity();
        for (size_t idx = 0; idx < active_det; ++idx) {
            double s = sum_image(det_imgs[idx]);
            if (s > best_sum) { best_sum = s; ref_idx = idx; }
        }
        if (!(best_sum > 0.0)) ref_idx = 0;
    }
    std::vector<std::pair<double,double>> shifts = estimate_detector_shifts_ref(det_imgs, ref_idx);

    // ----- Preprocess detector images -----
    // RAW (UNSHIFTED): used for mixture model (this fixes "flat foreground")
    std::vector<Image> raw_imgs; raw_imgs.reserve(active_det);
    for (size_t i = 0; i < active_det; ++i) {
        Image im = det_imgs[i];
        for (double& v : im.data) if (v < 0.0) v = 0.0;
        if (pre_blur_channels) box_blur_inplace(im, pre_blur_radius, 1);
        raw_imgs.emplace_back(std::move(im));
    }

    // APR (SHIFTED): optional, not used for fitting (kept for parity / external display)
    std::vector<Image> apr_imgs; apr_imgs.reserve(active_det);
    for (size_t i = 0; i < active_det; ++i) {
        const auto [dx, dy] = shifts[i];
        Image shifted = (std::abs(dx)>1e-9 || std::abs(dy)>1e-9)
                        ? subpixel_shift(det_imgs[i], dx, dy)
                        : det_imgs[i];
        for (double& v : shifted.data) if (v < 0.0) v = 0.0;
        if (pre_blur_channels) box_blur_inplace(shifted, pre_blur_radius, 1);
        apr_imgs.emplace_back(std::move(shifted));
    }

    // Totals for mixture & smoothing MUST come from RAW, not APR
    Image sum_img = sum_images(raw_imgs);

    // ----- Robust soft-gating scale (tau_soft) -----
    std::vector<double> totals_nonzero;
    totals_nonzero.reserve(W * H);
    for (double v : sum_img.data) if (v > 0.0) totals_nonzero.push_back(v);
    double tau_soft = 1.0;
    if (!totals_nonzero.empty()) {
        std::nth_element(totals_nonzero.begin(),
                         totals_nonzero.begin() + totals_nonzero.size()/2,
                         totals_nonzero.end());
        double med = totals_nonzero[totals_nonzero.size()/2];
        tau_soft = std::max(1.0, 0.25 * med); // 25% of median intensity
    }

    // ----- Calibration fingerprint (center patch) from RAW images -----
    size_t patch = static_cast<size_t>(std::max(1, calibration_size));
    patch = std::min({patch, W, H});
    size_t y0 = (H > patch) ? (H - patch) / 2 : 0;
    size_t x0 = (W > patch) ? (W - patch) / 2 : 0;

    std::vector<double> fingerprint(active_det, 0.0);
    for (size_t d = 0; d < active_det; ++d) {
        double acc = 0.0;
        const Image& det = raw_imgs[d]; // RAW!
        for (size_t y = 0; y < patch; ++y)
            for (size_t x = 0; x < patch; ++x)
                acc += det(y0 + y, x0 + x);
        fingerprint[d] = acc;
    }
    double max_fp = *std::max_element(fingerprint.begin(), fingerprint.end());
    if (max_fp > 0.0) for (double& v : fingerprint) v /= max_fp;

    // ----- Compute σ_A from fingerprint & radial2 -----
    double fp_sum = std::accumulate(fingerprint.begin(), fingerprint.end(), 0.0);
    double moment = 0.0;
    for (size_t i = 0; i < active_det; ++i) moment += fingerprint[i] * radial2[i];
    double sigmaA = 1.0;
    if (fp_sum > 0.0) sigmaA = std::sqrt(std::max(1e-6, moment / (2.0 * fp_sum)));
    if (!std::isfinite(sigmaA) || sigmaA < 1e-3) sigmaA = 1.0;

    // ----- σ bounds: background is wider than σ_A -----
    if (sigma_bound <= 0.0) sigma_bound = 2.0;
    double sigma_lower = std::max(sigma_min_abs, sigmaA);
    double sigma_upper = std::max(sigma_lower * 1.01, sigmaA * sigma_up_factor);

    std::vector<double> gaussA;
    normalized_gaussian(radial2, sigmaA, gaussA);

    // Output buffers
    Image Bmap(W, H, 0.0);
    Image focus_img(W, H, 0.0);
    Image background_img(W, H, 0.0);

    // Work buffers
    std::vector<double> micro(active_det, 0.0);
    std::vector<double> micro_norm(active_det, 0.0);
    std::vector<double> gauss_buffer(active_det, 0.0);

    auto sweep_sigma = [&](double min_sigma, double max_sigma, int steps,
                           const std::vector<double>& micro_n,
                           double& best_B, double& best_sigma, double& best_cost)
    {
        for (int si = 0; si < steps; ++si) {
            double t = (steps == 1) ? 0.5 : static_cast<double>(si) / (steps - 1);
            double ratio = max_sigma / std::max(min_sigma, 1e-6);
            if (ratio < 1.0) ratio = 1.0;
            double sigmaB = min_sigma * std::pow(ratio, t);
            normalized_gaussian(radial2, sigmaB, gauss_buffer);

            // LS estimate of B; can be swapped to NLL later
            double num = 0.0, den = 0.0;
            for (size_t i = 0; i < active_det; ++i) {
                double di = gauss_buffer[i] - gaussA[i];
                double resid = micro_n[i] - gaussA[i];
                num += di * resid;
                den += di * di;
            }
            double B = (den > eps_norm) ? (num / den) : 0.0;
            if (!std::isfinite(B)) B = 0.0;
            B = std::clamp(B, 0.0, 1.0);

            // SSE cost
            double sse = 0.0;
            for (size_t i = 0; i < active_det; ++i) {
                double di = gauss_buffer[i] - gaussA[i];
                double model = gaussA[i] + B * di;
                double diff = model - micro_n[i];
                sse += diff * diff;
            }

            if (sse < best_cost) {
                best_cost = sse;
                best_B = B;
                best_sigma = sigmaB;
            }
        }
    };

    // ----- Main per-pixel mixture fitting on RAW data -----
    for (size_t y = 0; y < H; ++y) {
        for (size_t x = 0; x < W; ++x) {
            double total = 0.0;
            for (size_t d = 0; d < active_det; ++d) {
                double v = raw_imgs[d](y, x);   // RAW!
                micro[d] = v;
                total += v;
            }

            if (total <= eps_norm) {
                Bmap(y, x) = 1.0;
                focus_img(y, x) = 0.0;
                background_img(y, x) = 0.0;
                continue;
            }

            double inv_tot = 1.0 / (total + eps_norm);
            for (size_t i = 0; i < active_det; ++i)
                micro_norm[i] = micro[i] * inv_tot;

            double best_B = 0.0;
            double best_sigma = sigmaA;
            double best_cost = std::numeric_limits<double>::infinity();

            sweep_sigma(sigma_lower, sigma_upper, coarse_steps, micro_norm, best_B, best_sigma, best_cost);
            double refine_lo = std::max(sigma_lower, best_sigma * 0.8);
            double refine_hi = std::min(sigma_upper, std::max(refine_lo * 1.01, best_sigma * 1.25));
            sweep_sigma(refine_lo, refine_hi, fine_steps, micro_norm, best_B, best_sigma, best_cost);

            // confidence weighting & shrinkage
            double w = total / (total + tau_soft);
            double lambda = lambda0 / std::sqrt(total + 1.0);
            double B_final = std::clamp(w * (best_B / (1.0 + lambda)), 0.0, 1.0);

            Bmap(y, x) = B_final;
            background_img(y, x) = total * B_final;
            focus_img(y, x) = total * (1.0 - B_final);
        }
    }

    // ----- Optional smoothing on B map (guided by RAW sum) -----
    if (B_smooth_iters > 0) {
        guided_smooth_B(Bmap, sum_img, B_smooth_iters);
        for (size_t i = 0; i < focus_img.data.size(); ++i) {
            double total = sum_img.data[i]; // RAW total
            double Bf = std::clamp(Bmap.data[i], 0.0, 1.0);
            background_img.data[i] = total * Bf;
            focus_img.data[i]      = total * (1.0 - Bf);
        }
    }

    // ----- Pack output -----
    if (nz < 1) nz = 1;
    *dim1 = nz;
    *dim2 = static_cast<int>(H);
    *dim3 = static_cast<int>(W);
    const size_t plane = H * W;
    const size_t total_entries = static_cast<size_t>(nz) * plane;
    double* buf = static_cast<double*>(std::malloc(total_entries * sizeof(double)));
    if (!buf) throw std::bad_alloc();

    std::copy(focus_img.data.begin(), focus_img.data.end(), buf);
    if (nz >= 2)
        std::copy(background_img.data.begin(), background_img.data.end(), buf + plane);
    for (int iz = 2; iz < nz; ++iz)
        std::copy(sum_img.data.begin(), sum_img.data.end(), buf + static_cast<size_t>(iz) * plane);

    *output = buf;
}


// --- Estimate shifts relative to reference detector ---
static std::vector<std::pair<double,double>> estimate_detector_shifts_ref(const std::vector<Image>& det_imgs, size_t ref_idx) {
    const size_t D = det_imgs.size();
    if (D==0) return {};
    std::vector<std::pair<double,double>> shifts(D, {0.0,0.0});
    if (ref_idx >= D) ref_idx = 0;
    const Image& ref = det_imgs[ref_idx];
    shifts[ref_idx] = {0.0, 0.0};
    // Relative shifts to reference
    for (size_t i=0;i<D;++i) {
        if (i == ref_idx) continue;
        auto s = phase_correlation_shift(det_imgs[i], ref);
        shifts[i] = s;
    }
    return shifts;
}

// --- NEW: Sheppard pre-sum per group (even/odd rows) ---
enum class LineGroup { Even, Odd, All };

// Build an image containing only lines from the requested group; others set to 0
static Image mask_lines_by_group(const Image& in, LineGroup group) {
    if (group==LineGroup::All) return in;
    Image out(in.W, in.H, 0.0);
    const bool want_even = (group==LineGroup::Even);
    for (size_t y=0; y<in.H; ++y) {
        const bool is_even = ((y & 1u)==0u);
        if (is_even == want_even) {
            const size_t row_off = y*in.W;
            std::copy(in.data.begin()+row_off, in.data.begin()+row_off+in.W, out.data.begin()+row_off);
        }
    }
    return out;
}

static Image sheppard_group_sum(const std::vector<Image>& det_imgs,
                                const std::vector<std::pair<double,double>>& shifts,
                                LineGroup group)
{
    if (det_imgs.empty() || det_imgs.size()!=shifts.size())
        throw std::runtime_error("sheppard_group_sum: mismatched inputs");
    const size_t W = det_imgs[0].W, H = det_imgs[0].H;
    Image acc(W,H,0.0);
    for (size_t i=0;i<det_imgs.size();++i) {
        Image masked = mask_lines_by_group(det_imgs[i], group);
        const auto [dx,dy] = shifts[i];
        Image shifted = (std::abs(dx)>1e-9 || std::abs(dy)>1e-9) ? subpixel_shift(masked, dx, dy) : masked;
        for (size_t k=0;k<acc.data.size();++k) acc.data[k] += shifted.data[k];
    }
    return acc;
}

static Image shift_image(const Image& in, double dx, double dy) {
    return subpixel_shift(in, dx, dy);
}

} // anonymous namespace


void CLSMISM::apr_reconstruction(
    const double* data, int dim0, int dim1, int dim2,
    bool channels_last,
    double** output, int* out_dim1, int* out_dim2, int* out_dim3,
    int usf, int ref_idx, double filter_sigma,
    int nz, int n_det)
{
    if (!output || !out_dim1 || !out_dim2 || !out_dim3) {
        throw std::runtime_error("apr_reconstruction: null output/dims");
    }
    if (dim0 <= 0 || dim1 <= 0 || dim2 <= 0) {
        throw std::runtime_error("apr_reconstruction: invalid dimensions");
    }

    std::vector<Image> det_imgs = detector_images_from_array(
        data,
        static_cast<size_t>(dim0),
        static_cast<size_t>(dim1),
        static_cast<size_t>(dim2),
        channels_last);

    if (n_det > 0 && static_cast<size_t>(n_det) < det_imgs.size()) {
        det_imgs.resize(static_cast<size_t>(n_det));
    }

    apr_reconstruction_core(det_imgs, output, out_dim1, out_dim2, out_dim3, usf, ref_idx, filter_sigma, nz);
}

double* CLSMISM::apr_reconstruction(
    const double* data,
    int dim0, int dim1, int dim2,
    bool channels_last,
    int usf, int ref_idx, double filter_sigma,
    int nz, int n_det)
{
    double* out = nullptr;
    int d1, d2, d3;
    apr_reconstruction(data, dim0, dim1, dim2, channels_last, &out, &d1, &d2, &d3, usf, ref_idx, filter_sigma, nz, n_det);
    return out;
}

void CLSMISM::focus_reconstruction(
    const double* data,
    int dim0, int dim1, int dim2,
    bool channels_last,
    double** output, int* out_dim1, int* out_dim2, int* out_dim3,
    double sigma_bound, double threshold, int calibration_size,
    bool parallelize,
    int nz, int n_det,
    const double* detector_coords, int detector_coords_len)
{
    if (!output || !out_dim1 || !out_dim2 || !out_dim3) {
        throw std::runtime_error("focus_ism_reconstruction: null output/dims");
    }
    if (dim0 <= 0 || dim1 <= 0 || dim2 <= 0) {
        throw std::runtime_error("focus_ism_reconstruction: invalid dimensions");
    }

    std::vector<Image> det_imgs = detector_images_from_array(
        data,
        static_cast<size_t>(dim0),
        static_cast<size_t>(dim1),
        static_cast<size_t>(dim2),
        channels_last);

    if (n_det > 0 && static_cast<size_t>(n_det) < det_imgs.size()) {
        det_imgs.resize(static_cast<size_t>(n_det));
    }

    focus_ism_core(det_imgs, sigma_bound, threshold, calibration_size, parallelize, nz, output, out_dim1, out_dim2, out_dim3, detector_coords, detector_coords_len);
}

double* CLSMISM::focus_reconstruction(
    const double* data,
    int dim0, int dim1, int dim2,
    bool channels_last,
    double sigma_bound, double threshold, int calibration_size,
    bool parallelize,
    int nz, int n_det,
    const double* detector_coords, int detector_coords_len)
{
    double* out = nullptr;
    int d1, d2, d3;
    focus_reconstruction(data, dim0, dim1, dim2, channels_last, &out, &d1, &d2, &d3, sigma_bound, threshold, calibration_size, parallelize, nz, n_det, detector_coords, detector_coords_len);
    return out;
}


