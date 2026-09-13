#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// Minimal 2D convex geometry used by the multipoint generator.
// Mirrors the operations Fatality performs on its projected hitbox points:
// convex hull (Graham/monotone chain) + convex polygon intersection
// (Sutherland-Hodgman) + area/centroid.
namespace multipoint_geom
{
	struct point2_t
	{
		float x{};
		float y{};
	};

	INLINE float cross(const point2_t& o, const point2_t& a, const point2_t& b)
	{
		return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
	}

	// Andrew's monotone chain. Produces a counter-clockwise convex hull.
	INLINE void convex_hull(std::vector<point2_t>& pts)
	{
		if (pts.size() < 3)
			return;

		std::sort(pts.begin(), pts.end(), [](const point2_t& a, const point2_t& b) {
			return a.x < b.x || (a.x == b.x && a.y < b.y);
			});

		std::vector<point2_t> hull(pts.size() * 2);
		int k = 0;

		for (int i = 0; i < static_cast<int>(pts.size()); ++i)
		{
			while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0.f)
				--k;

			hull[k++] = pts[i];
		}

		for (int i = static_cast<int>(pts.size()) - 2, t = k + 1; i >= 0; --i)
		{
			while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0.f)
				--k;

			hull[k++] = pts[i];
		}

		hull.resize(k - 1);
		pts = std::move(hull);
	}

	// Clips `subject` by the convex, CCW `clip` polygon.
	INLINE std::vector<point2_t> intersect(const std::vector<point2_t>& subject, const std::vector<point2_t>& clip)
	{
		if (subject.size() < 3 || clip.size() < 3)
			return {};

		std::vector<point2_t> output = subject;

		for (size_t i = 0; i < clip.size() && output.size() >= 3; ++i)
		{
			const point2_t& a = clip[i];
			const point2_t& b = clip[(i + 1) % clip.size()];
			const std::vector<point2_t> input = output;
			output.clear();

			auto inside = [&](const point2_t& p) { return cross(a, b, p) >= 0.f; };

			auto line_intersect = [&](const point2_t& p1, const point2_t& p2) -> point2_t {
				const float a1 = p2.y - p1.y, b1 = p1.x - p2.x, c1 = a1 * p1.x + b1 * p1.y;
				const float a2 = b.y - a.y, b2 = a.x - b.x, c2 = a2 * a.x + b2 * a.y;
				const float det = a1 * b2 - a2 * b1;

				if (std::fabs(det) < 1e-9f)
					return p2;

				return { (b2 * c1 - b1 * c2) / det, (a1 * c2 - a2 * c1) / det };
			};

			for (size_t j = 0; j < input.size(); ++j)
			{
				const point2_t& cur = input[j];
				const point2_t& prev = input[(j + input.size() - 1) % input.size()];
				const bool cur_in = inside(cur);
				const bool prev_in = inside(prev);

				if (cur_in)
				{
					if (!prev_in)
						output.push_back(line_intersect(prev, cur));

					output.push_back(cur);
				}
				else if (prev_in)
					output.push_back(line_intersect(prev, cur));
			}
		}

		return output.size() >= 3 ? output : std::vector<point2_t>{};
	}

	INLINE float area(const std::vector<point2_t>& poly)
	{
		if (poly.size() < 3)
			return 0.f;

		float a = 0.f;
		for (size_t i = 0; i < poly.size(); ++i)
		{
			const point2_t& p = poly[i];
			const point2_t& q = poly[(i + 1) % poly.size()];
			a += p.x * q.y - q.x * p.y;
		}

		return std::fabs(a) * 0.5f;
	}

	INLINE point2_t centroid(const std::vector<point2_t>& poly)
	{
		point2_t c{};

		if (poly.empty())
			return c;

		for (const auto& p : poly)
		{
			c.x += p.x;
			c.y += p.y;
		}

		c.x /= static_cast<float>(poly.size());
		c.y /= static_cast<float>(poly.size());
		return c;
	}

	struct extremes_t
	{
		point2_t left{};
		point2_t right{};
		point2_t top{};
		point2_t bottom{};
	};

	INLINE extremes_t extremes(const std::vector<point2_t>& poly)
	{
		extremes_t out{};

		if (poly.empty())
			return out;

		out.left = out.right = out.top = out.bottom = poly[0];

		for (const auto& p : poly)
		{
			if (p.x < out.left.x)
				out.left = p;
			else if (p.x > out.right.x)
				out.right = p;

			if (p.y > out.top.y)
				out.top = p;
			else if (p.y < out.bottom.y)
				out.bottom = p;
		}

		return out;
	}

	INLINE point2_t lerp(const point2_t& a, const point2_t& b, float t)
	{
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
	}
}
