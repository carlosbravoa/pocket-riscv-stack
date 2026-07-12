/*
 * math_shim.c — soft-float math for the Wolf4SDL riscv-stack port (copied from sdk/tyrian).
 *
 * The CPU is rv32im (no FPU) and LiteX's picolibc-minimal ships no libm, so
 * the handful of math calls the game makes (starfield trig, volume tables,
 * destruct-mode ballistics) get small polynomial implementations here.
 * Accuracy is ~1e-5 relative — plenty for cosmetic game math; nothing
 * numerical depends on these.
 *
 * GPL-2.0-or-later (port glue; see compat/SDL.h).
 */
#include <stdint.h>

float  fabsf(float x);
float  floorf(float x);
float  roundf(float x);
float  sqrtf(float x);
float  sinf(float x);
float  cosf(float x);
float  expf(float x);
float  logf(float x);
float  powf(float x, float y);
double sin(double x);
double cos(double x);
double pow(double x, double y);
double sqrt(double x);
double floor(double x);
double fabs(double x);
double round(double x);

float fabsf(float x)
{
	union { float f; uint32_t u; } v = { x };
	v.u &= 0x7FFFFFFFu;
	return v.f;
}

double fabs(double x) { return x < 0 ? -x : x; }

float floorf(float x)
{
	int32_t i = (int32_t)x;             /* trunc toward zero */
	return (x < 0 && (float)i != x) ? (float)(i - 1) : (float)i;
}

double floor(double x)
{
	int64_t i = (int64_t)x;
	return (x < 0 && (double)i != x) ? (double)(i - 1) : (double)i;
}

float roundf(float x)
{
	return (x >= 0) ? floorf(x + 0.5f) : -floorf(-x + 0.5f);
}

double round(double x)
{
	return (x >= 0) ? floor(x + 0.5) : -floor(-x + 0.5);
}

float sqrtf(float x)
{
	if (x <= 0)
		return 0;
	/* initial guess via exponent halving, then 4 Newton steps */
	union { float f; uint32_t u; } v = { x };
	v.u = (v.u >> 1) + 0x1FBD1DF5u;
	float y = v.f;
	for (int i = 0; i < 4; i++)
		y = 0.5f * (y + x / y);
	return y;
}

double sqrt(double x) { return (double)sqrtf((float)x); }

/* sin on [-pi, pi] via odd minimax-ish polynomial (Taylor 7/9 terms). */
static float sin_poly(float x)
{
	float x2 = x * x;
	return x * (1.0f + x2 * (-1.6666667e-1f + x2 * (8.3333310e-3f +
	       x2 * (-1.98409e-4f + x2 * 2.7526e-6f))));
}

#define RV_PI  3.14159265358979f
#define RV_2PI 6.28318530717959f

static float range_reduce(float x)
{
	/* reduce to [-pi, pi] */
	float k = floorf((x + RV_PI) / RV_2PI);
	return x - k * RV_2PI;
}

float sinf(float x)
{
	x = range_reduce(x);
	/* fold onto [-pi/2, pi/2] where the polynomial is accurate */
	if (x > RV_PI / 2)
		x = RV_PI - x;
	else if (x < -RV_PI / 2)
		x = -RV_PI - x;
	return sin_poly(x);
}

float cosf(float x) { return sinf(x + RV_PI / 2); }

double sin(double x) { return (double)sinf((float)x); }
double cos(double x) { return (double)cosf((float)x); }

/* exp2-based expf: x/ln2 = k + f, e^x = 2^k * 2^f, f in [-0.5, 0.5] */
float expf(float x)
{
	if (x > 88.0f)
		return 3.4e38f;
	if (x < -87.0f)
		return 0.0f;
	float t = x * 1.442695041f;         /* x / ln2 */
	float k = floorf(t + 0.5f);
	float f = t - k;                    /* [-0.5, 0.5] */
	/* 2^f = e^(f*ln2), Taylor to x^5 */
	float z = f * 0.6931471806f;
	float p = 1.0f + z * (1.0f + z * (0.5f + z * (1.0f / 6 + z * (1.0f / 24 + z / 120))));
	union { float f; uint32_t u; } v;
	v.u = (uint32_t)(((int32_t)k + 127) << 23);   /* 2^k */
	return p * v.f;
}

float logf(float x)
{
	if (x <= 0)
		return -87.0f;                  /* game never feeds 0/neg on purpose */
	union { float f; uint32_t u; } v = { x };
	int32_t e = (int32_t)((v.u >> 23) & 0xFF) - 127;
	v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;      /* mantissa in [1,2) */
	float m = v.f;
	if (m > 1.4142136f) {               /* normalize to [1/sqrt2, sqrt2) */
		m *= 0.5f;
		e += 1;
	}
	float z = (m - 1.0f) / (m + 1.0f);  /* atanh series: ln(m)=2*atanh(z) */
	float z2 = z * z;
	float l = 2.0f * z * (1.0f + z2 * (1.0f / 3 + z2 * (1.0f / 5 + z2 / 7)));
	return l + (float)e * 0.6931471806f;
}

float atanf(float x)
{
	/* |x|<=1: minimax-ish odd polynomial; else fold via pi/2 - atan(1/x) */
	int inv = 0, neg = x < 0;
	if (neg)
		x = -x;
	if (x > 1.0f) {
		x = 1.0f / x;
		inv = 1;
	}
	float x2 = x * x;
	float r = x * (0.9998660f + x2 * (-0.3302995f + x2 * (0.1801410f +
	          x2 * (-0.0851330f + x2 * 0.0208351f))));
	if (inv)
		r = 1.5707963f - r;
	return neg ? -r : r;
}

double atan(double x) { return (double)atanf((float)x); }

float atan2f(float y, float x)
{
	if (x > 0)
		return atanf(y / x);
	if (x < 0)
		return atanf(y / x) + (y >= 0 ? RV_PI : -RV_PI);
	return (y >= 0) ? RV_PI / 2 : -RV_PI / 2;
}

float powf(float x, float y)
{
	if (y == 0.0f)
		return 1.0f;
	if (x == 0.0f)
		return 0.0f;
	if (x < 0.0f) {
		/* negative base: only sane for integral exponents */
		int32_t iy = (int32_t)y;
		float r = expf(y * logf(-x));
		return (iy & 1) ? -r : r;
	}
	return expf(y * logf(x));
}

double pow(double x, double y) { return (double)powf((float)x, (float)y); }

// atan2 for boss projectile aim (occasional; soft-float is fine).
// Rational approximation of atan on [0,1], quadrant-corrected; ~1e-3 rad.
static double atan_unit(double z)
{
	return z * (0.9724 - 0.1919 * z * z);   /* max err ~5e-3 rad */
}
double atan2(double y, double x)
{
	const double PI = 3.14159265358979;
	double ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
	double a = (ay <= ax) ? atan_unit(ax == 0 ? 0 : ay / ax)
	                      : PI / 2 - atan_unit(ax / ay);
	if (x < 0)
		a = PI - a;
	return y < 0 ? -a : a;
}

// tan for BuildTables (init-time only): sin/cos are already shimmed.
double tan(double x) { return sin(x) / cos(x); }
