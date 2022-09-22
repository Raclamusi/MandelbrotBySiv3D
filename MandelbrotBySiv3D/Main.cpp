# include <Siv3D.hpp> // OpenSiv3D v0.6.5
# include <complex>

template <std::floating_point T>
[[nodiscard]]
std::size_t IterateMandelbrot(std::complex<T> c, std::size_t maxIteration)
{
	std::complex<T> z = 0;
	for (auto&& i : step(maxIteration))
	{
		z *= z;
		z += c;
		if (IsInfinity(z.real()) || IsInfinity(z.imag()))
		{
			// 発散！
			return i;
		}
	}
	return maxIteration;
}

void MakeMandelbrotImageImpl(Image& image, const Rect& rect, const RectF& region, std::size_t maxIteration, std::stop_token st)
{
	for (auto&& pos : step(rect.pos, rect.size))
	{
		if (st.stop_requested()) [[unlikely]]
		{
			return;
		}
		auto&& [cr, ci] = region.pos + pos * region.size / (image.size() - Size::One());
		image[pos] = HSV{ IterateMandelbrot(cr + 1i * ci, maxIteration) * 300.0 / maxIteration - 60 };
	}
}

void MakeMandelbrotImage(Image& image, const RectF& region, std::size_t maxIteration, std::size_t numOfThread = Threading::GetConcurrency(), std::stop_token st = {})
{
	if (numOfThread == 0)
	{
		return;
	}
	Array<Rect> rects(Arg::reserve = numOfThread);
	{
		std::size_t y = 0;
		std::size_t h = 0;
		for (auto&& i : step(numOfThread))
		{
			y += h;
			h = image.height() * (i + 1) / numOfThread - y;
			rects.emplace_back(0, y, image.width(), h);
		}
	}
	{
		Array<std::jthread> threads(Arg::reserve = numOfThread - 1);
		for (auto&& i : step(numOfThread - 1))
		{
			threads.emplace_back([&, i] {
				MakeMandelbrotImageImpl(image, rects[i], region, maxIteration, st);
			});
		}
		MakeMandelbrotImageImpl(image, rects.back(), region, maxIteration, st);
	}
}

void Main()
{
	Window::Resize(1920, 1080);

	Image image{ Scene::Size() };
	Image smallImage{ Scene::Size() / 4 };
	DynamicTexture texture{ image.size() };
	DynamicTexture smallTexture{ smallImage.size() };
	auto region = RectF{ Scene::Rect().horizontalAspectRatio(), 1 }.scaled(3).setCenter(Vec2::Zero());
	std::jthread makingThread;
	bool update = true;
	bool smallUpdate = true;
	bool small = true;

	while (System::Update())
	{
		if (MouseL.pressed() && not Cursor::Delta().isZero())
		{
			// 移動
			region.moveBy(-Cursor::DeltaF() * region.size / Scene::Size());
			smallUpdate = true;
		}
		if (Mouse::Wheel() != 0)
		{
			// 拡大・縮小
			region.moveBy(Cursor::PosF() * region.size / Scene::Size());
			region.size *= Math::Pow(KeyShift.pressed() ? 1.3 : 1.1, Mouse::Wheel());
			region.moveBy(-Cursor::PosF() * region.size / Scene::Size());
			smallUpdate = true;
		}

		if (smallUpdate)
		{
			// 変更があったら smallImage を更新
			smallUpdate = false;
			update = true;
			MakeMandelbrotImage(smallImage, region, 100);
			smallTexture.fill(smallImage);
			small = true;
			makingThread = std::jthread{};
		}
		else if (update)
		{
			// 今フレームでの変更がなくて前フレームでの変更があったら image の更新を開始
			update = false;
			makingThread = std::jthread{ [&](std::stop_token st) {
				using namespace std::chrono;
				auto start = steady_clock::now();
				// メインの処理を邪魔しないようにスレッドは半分だけ使う
				MakeMandelbrotImage(image, region, 100, Threading::GetConcurrency() / 2, st);
				auto end = steady_clock::now();
				if (not st.stop_requested())
				{
					texture.fill(image);
					small = false;
					Print << U"update: " << duration_cast<milliseconds>(end - start);
				}
			} };
		}
		(small ? smallTexture : texture).resized(Scene::Size()).draw();
	}
}
