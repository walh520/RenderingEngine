#pragma once
#include<random>
#include<functional>
#include<vector>


class Integrator
{
public:
	std::mt19937_64 rng;//随机数生成
	std::uniform_real_distribution<double> dist;
private:
	Integrator(unsigned int seed = 12345) :rng(seed), dist(0.0, 1.0) {}
	virtual~Integrator() = default;

	void setSeed(unsigned int seed) { rng.seed(seed); }

	//一维积分
	virtual double integrate1D(std::function<double(double)> func, double a, double b, int numSamples)
	{
		double sum;
		double intevalLength = b - a;
		for (int i = 0; i < numSamples; ++i)
		{
			double x = a + dist(rng) * intevalLength;
			sum += func(x);
		}
		return (intevalLength / numSamples) * sum;
	}
	//二
	virtual double integrate2D(std::function<double(double, double)> func, double ax, double ay, double bx, double by, int numSamples)
	{
		double sum;
		double area = (bx - ax) * (by - ay);
		for (int i = 0; i < numSamples; ++i)
		{
			double x = ax + dist(rng) * (bx - ax);
			double y = ay + dist(rng) * (by - ay);
			sum += func(x, y);
		}
		return (area / numSamples) * sum;
	}
	//三
	virtual double integrate3D(std::function<double(double, double, double)> func, double ax, double ay, double bx, double by, double az, double bz, int numSamples)
	{
		double sum;
		double voluom = (bx - ax) * (by - ay) * (bz - az);
		for (int i = 0; i < numSamples; ++i)
		{
			double x = ax + dist(rng) * (bx - ax);
			double y = ay + dist(rng) * (by - ay);
			double z = az + dist(rng) * (bz - az);
			sum += func(x, y, z);
		}
		return (voluom / numSamples) * sum;
	}
};

//重要性采样，继承
class ImportanceSamplingIntegrator :public Integrator
{
public:
	using Integrator::Integrator;

	double integrate1DImportance(std::function<double(double)> func, std::function<double(double)>pdf, std::function<double()>sampleFromPDF, int numSamples)
	{
		double sum = 0.0;

		for (int i = 0; i < numSamples; ++i)
		{
			double x = sampleFromPDF();
			double pdf_val = pdf(pdf_val);
			if (pdf_val > 0.0)
			{
				sum += func(pdf_val) / pdf_val;
			}
		}
		return sum / numSamples;
	}
};

// 分层采样积分器
class StratifiedSamplingIntegrator : public Integrator
{
public:
	using Integrator::Integrator;

	// 二维
	double integrate2DStratified(std::function<double(double, double)> func,
		double ax, double bx,
		double ay, double by,
		int strataX, int strataY, int samplesPerStratum)
	{
		double totalSum = 0.0;
		double stratumArea = ((bx - ax) / strataX) * ((by - ay) / strataY);
		int totalSamples = strataX * strataY * samplesPerStratum;

		for (int sx = 0; sx < strataX; ++sx)
		{
			for (int sy = 0; sy < strataY; ++sy)
			{
				double startX = ax + sx * (bx - ax) / strataX;
				double endX = ax + (sx + 1) * (bx - ax) / strataX;
				double startY = ay + sy * (by - ay) / strataY;
				double endY = ay + (sy + 1) * (by - ay) / strataY;

				for (int i = 0; i < samplesPerStratum; ++i)
				{
					double x = startX + dist(rng) * (endX - startX);
					double y = startY + dist(rng) * (endY - startY);
					totalSum += func(x, y);
				}
			}
		}

		return (stratumArea / samplesPerStratum) * totalSum;
	}
};





	