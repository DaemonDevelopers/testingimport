#define SRGB_MIX

uniform int u_LinearizeTexture;

void convertFromSRGB(inout vec3 color, in bool condition) {
	if (condition)
	{
	#if defined(r_cheapSRGB)
		float gamma = 2.2;
		color = pow(color, vec3(gamma));

	#elif defined(SRGB_NAIVE)
		// (((c) <= 0.04045f) ? (c) * (1.0f / 12.92f) : (float)pow(((c) + 0.055f)*(1.0f/1.055f), 2.4f))

		float threshold = 0.0031308f;

		color.r = color.r <= threshold ? color.r * (1.0f / 12.92f) : pow((color.r + 0.055f) * (1.0f / 1.055f), 2.4f);
		color.g = color.g <= threshold ? color.g * (1.0f / 12.92f) : pow((color.g + 0.055f) * (1.0f / 1.055f), 2.4f);
		color.b = color.b <= threshold ? color.b * (1.0f / 12.92f) : pow((color.b + 0.055f) * (1.0f / 1.055f), 2.4f);
	#elif defined(SRGB_FBOOL)
		float threshold = 0.0031308f;

		vec3 yes = vec3(float(color.r <= threshold), float(color.g <= threshold), float(color.b <= threshold));
		vec3 no = yes * -1.0f + 1.0f;

		vec3 low = color * (1.0f / 12.92f);
		vec3 high = pow((color + 0.055f) * (1.0f / 1.055f), vec3(2.4f));

		color = (yes * low) + (no * high);
	#elif defined(SRGB_BOOL)
		float threshold = 0.0031308f;

		bvec3 yes = bvec3(color.r <= threshold, color.g <= threshold, color.b <= threshold);
		bvec3 no = bvec3(!yes.x, !yes.y, !yes.z);

		vec3 low = color * (1.0f / 12.92f);
		vec3 high = pow((color + 0.055f) * (1.0f / 1.055f), vec3(2.4f));

		color = (float(yes) * low) + (float(no) * high);
	#elif defined(SRGB_MIX)
		float threshold = 0.0031308f;

		bvec3 cutoff = lessThan(color, vec3(threshold));
		vec3 low = color / vec3(12.92f);
		vec3 high = pow((color + vec3(0.055f)) / vec3(1.055f), vec3(2.4f));

		color = mix(high, low, cutoff);
	#else
		#error undefined SRGB computation
	#endif
	}
}

void convertFromSRGB(inout vec3 color, in int condition) {
	convertFromSRGB(color, condition != 0);
}

void convertToSRGB(inout vec3 color, in bool condition) {
	if (condition)
	{
	#if defined(r_cheapSRGB)
		float gamma = 2.2;
		color = pow(color, vec3(1/gamma));
	#elif defined(SRGB_NAIVE)
		// (((c) < 0.0031308f) ? (c) * 12.92f : 1.055f * (float)pow((c), 1.0f/2.4f) - 0.055f)
		float threshold = 0.0031308f;

		color.r = color.r < threshold ? color.r * 12.92f : 1.055f * pow(color.r, 1.0f / 2.4f) - 0.055f;
		color.g = color.g < threshold ? color.g * 12.92f : 1.055f * pow(color.g, 1.0f / 2.4f) - 0.055f;
		color.b = color.b < threshold ? color.b * 12.92f : 1.055f * pow(color.b, 1.0f / 2.4f) - 0.055f;
	#elif defined(SRGB_FBOOL)
		float threshold = 0.0031308f;

		vec3 yes = vec3(float(color.r < threshold), float(color.g < threshold), float(color.b < threshold));
		vec3 no = yes * -1.0f + 1.0f;

		vec3 low = color * 12.92f;
		vec3 high = pow(color, vec3(1.0f / 2.4f)) - 0.055f;

		color = (yes * low) + (no * high);
	#elif defined(SRGB_BOOL)
		float threshold = 0.0031308f;

		bvec3 yes = bvec3(color.r < threshold, color.g < threshold, color.b < threshold);
		bvec3 no = bvec3(!yes.x, !yes.y, !yes.z);

		vec3 low = color * 12.92f;
		vec3 high = pow(color, vec3(1.0f / 2.4f)) - 0.055f;

		color = (float(yes) * low) + (float(no) * high);
	#elif defined(SRGB_MIX)
		float threshold = 0.0031308f;

		bvec3 cutoff = lessThan(color, vec3(threshold));
		vec3 low = vec3(12.92f) * color;
		vec3 high = vec3(1.055f) * pow(color, vec3(1.0f / 2.4f)) - vec3(0.055f);

		color = mix(high, low, cutoff);
	#else
		#error undefined SRGB computation
	#endif
	}
}

void convertToSRGB(inout vec3 color, in int condition) {
	convertToSRGB(color, condition != 0);
}
