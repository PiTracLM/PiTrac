/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

/*
The comments in this code refer to:
Michele Fornaciari, Andrea Prati, Rita Cucchiara,
A fast and effective ellipse detector for embedded vision applications
Pattern Recognition, Volume 47, Issue 11, November 2014, Pages 3693-3708, ISSN 0031-3203,
http://dx.doi.org/10.1016/j.patcog.2014.05.012.
(http://www.sciencedirect.com/science/article/pii/S0031320314001976)
*/

#include "EllipseDetectorYaed.h"
#include "logging_tools.h"

namespace golf_sim {

CEllipseDetectorYaed::CEllipseDetectorYaed(void) : _times(6, 0.0), _timesHelper(6, 0.0)
{
	// Default Parameters Settings
	_szPreProcessingGaussKernelSize = Size(5, 5);
	_dPreProcessingGaussSigma = 1.0;
	_fThPosition = 1.0f;
	_fMaxCenterDistance = 100.0f * 0.05f;
	_fMaxCenterDistance2 = _fMaxCenterDistance * _fMaxCenterDistance;
	_iMinEdgeLength = 16;
	_fMinOrientedRectSide = 3.0f;
	_fDistanceToEllipseContour = 0.1f;
	_fMinScore = 0.4f;
	_fMinReliability = 0.4f;
	_uNs = 16;

	srand(unsigned(time(nullptr)));
}


CEllipseDetectorYaed::~CEllipseDetectorYaed(void)
{
}

void CEllipseDetectorYaed::SetParameters(Size	szPreProcessingGaussKernelSize,
	double	dPreProcessingGaussSigma,
	float 	fThPosition,
	float	fMaxCenterDistance,
	int		iMinEdgeLength,
	float	fMinOrientedRectSide,
	float	fDistanceToEllipseContour,
	float	fMinScore,
	float	fMinReliability,
	int     iNs
	)
{
	_szPreProcessingGaussKernelSize = szPreProcessingGaussKernelSize;
	_dPreProcessingGaussSigma = dPreProcessingGaussSigma;
	_fThPosition = fThPosition;
	_fMaxCenterDistance = fMaxCenterDistance;
	_iMinEdgeLength = iMinEdgeLength;
	_fMinOrientedRectSide = fMinOrientedRectSide;
	_fDistanceToEllipseContour = fDistanceToEllipseContour;
	_fMinScore = fMinScore;
	_fMinReliability = fMinReliability;
	_uNs = iNs;

	_fMaxCenterDistance2 = _fMaxCenterDistance * _fMaxCenterDistance;

}

uint inline CEllipseDetectorYaed::GenerateKey(uchar pair, ushort u, ushort v)
{
	return (pair << 30) + (u << 15) + v;
};



int CEllipseDetectorYaed::FindMaxK(const int* v) const
{
	int max_val = 0;
	int max_idx = 0;
	for (int i = 0; i<ACC_R_SIZE; ++i)
	{
		(v[i] > max_val) ? max_val = v[i], max_idx = i : 0;
	}

	return max_idx + 90;
};

int CEllipseDetectorYaed::FindMaxN(const int* v) const
{
	int max_val = 0;
	int max_idx = 0;
	for (int i = 0; i<ACC_N_SIZE; ++i)
	{
		(v[i] > max_val) ? max_val = v[i], max_idx = i : 0;
	}

	return max_idx;
};

int CEllipseDetectorYaed::FindMaxA(const int* v) const
{
	int max_val = 0;
	int max_idx = 0;
	for (int i = 0; i<ACC_A_SIZE; ++i)
	{
		(v[i] > max_val) ? max_val = v[i], max_idx = i : 0;
	}

	return max_idx;
};


float CEllipseDetectorYaed::GetMedianSlope(vector<Point2f>& med, Point2f& M, vector<float>& slopes)
{
	// med		: vector of points
	// M		: centroid of the points in med
	// slopes	: vector of the slopes

	unsigned iNofPoints = (unsigned)med.size();
	//CV_Assert(iNofPoints >= 2);

	unsigned halfSize = iNofPoints >> 1;
	unsigned quarterSize = halfSize >> 1;

	vector<float> xx, yy;
	slopes.reserve(halfSize);
	xx.reserve(iNofPoints);
	yy.reserve(iNofPoints);

	for (unsigned i = 0; i < halfSize; ++i)
	{
		Point2f& p1 = med[i];
		Point2f& p2 = med[halfSize + i];

		xx.push_back(p1.x);
		xx.push_back(p2.x);
		yy.push_back(p1.y);
		yy.push_back(p2.y);

		float den = (p2.x - p1.x);
		float num = (p2.y - p1.y);

		if (den == 0) den = 0.00001f;

		slopes.push_back(num / den);
	}

	nth_element(slopes.begin(), slopes.begin() + quarterSize, slopes.end());
	nth_element(xx.begin(), xx.begin() + halfSize, xx.end());
	nth_element(yy.begin(), yy.begin() + halfSize, yy.end());
	M.x = xx[halfSize];
	M.y = yy[halfSize];

	return slopes[quarterSize];
};




void CEllipseDetectorYaed::GetFastCenter(vector<Point>& e1, vector<Point>& e2, EllipseData& data)
{
	data.isValid = true;

	unsigned size_1 = unsigned(e1.size());
	unsigned size_2 = unsigned(e2.size());

	unsigned hsize_1 = size_1 >> 1;
	unsigned hsize_2 = size_2 >> 1;

	Point& med1 = e1[hsize_1];
	Point& med2 = e2[hsize_2];

	Point2f M12, M34;
	float q2, q4;

	{
		// First to second

		// Reference slope

		float dx_ref = float(e1[0].x - med2.x);
		float dy_ref = float(e1[0].y - med2.y);

		if (dy_ref == 0) dy_ref = 0.00001f;

		float m_ref = dy_ref / dx_ref;
		data.ra = m_ref;

		// Find points with same slope as reference
		vector<Point2f> med;
		med.reserve(hsize_2);

		unsigned minPoints = (_uNs < hsize_2) ? _uNs : hsize_2;

		vector<uint> indexes(minPoints);
		if (_uNs < hsize_2)
		{
			unsigned iSzBin = hsize_2 / unsigned(_uNs);
			unsigned iIdx = hsize_2 + (iSzBin / 2);

			for (unsigned i = 0; i<_uNs; ++i)
			{
				indexes[i] = iIdx;
				iIdx += iSzBin;
			}
		}
		else
		{
			iota(indexes.begin(), indexes.end(), hsize_2);
		}



		for (uint ii = 0; ii<minPoints; ++ii)
		{
			uint i = indexes[ii];

			float x1 = float(e2[i].x);
			float y1 = float(e2[i].y);

			uint begin = 0;
			uint end = size_1 - 1;

			float xb = float(e1[begin].x);
			float yb = float(e1[begin].y);
			float res_begin = ((xb - x1) * dy_ref) - ((yb - y1) * dx_ref);
			int sign_begin = sgn(res_begin);
			if (sign_begin == 0)
			{
				//found
				med.push_back(Point2f((xb + x1)* 0.5f, (yb + y1)* 0.5f));
				continue;
			}

			float xe = float(e1[end].x);
			float ye = float(e1[end].y);
			float res_end = ((xe - x1) * dy_ref) - ((ye - y1) * dx_ref);
			int sign_end = sgn(res_end);
			if (sign_end == 0)
			{
				//found
				med.push_back(Point2f((xe + x1)* 0.5f, (ye + y1)* 0.5f));
				continue;
			}

			if ((sign_begin + sign_end) != 0)
			{
				continue;
			}

			uint j = (begin + end) >> 1;

			while (end - begin > 2)
			{
				float x2 = float(e1[j].x);
				float y2 = float(e1[j].y);
				float res = ((x2 - x1) * dy_ref) - ((y2 - y1) * dx_ref);
				int sign_res = sgn(res);

				if (sign_res == 0)
				{
					//found
					med.push_back(Point2f((x2 + x1)* 0.5f, (y2 + y1)* 0.5f));
					break;
				}

				if (sign_res + sign_begin == 0)
				{
					sign_end = sign_res;
					end = j;
				}
				else
				{
					sign_begin = sign_res;
					begin = j;
				}
				j = (begin + end) >> 1;
			}

			med.push_back(Point2f((e1[j].x + x1)* 0.5f, (e1[j].y + y1)* 0.5f));
		}

		if (med.size() < 2)
		{
			data.isValid = false;
			return;
		}

		q2 = GetMedianSlope(med, M12, data.Sa);
	}

	{
		// Second to first

		// Reference slope
		float dx_ref = float(med1.x - e2[0].x);
		float dy_ref = float(med1.y - e2[0].y);

		if (dy_ref == 0) dy_ref = 0.00001f;

		float m_ref = dy_ref / dx_ref;
		data.rb = m_ref;

		// Find points with same slope as reference
		vector<Point2f> med;
		med.reserve(hsize_1);

		uint minPoints = (_uNs < hsize_1) ? _uNs : hsize_1;

		vector<uint> indexes(minPoints);
		if (_uNs < hsize_1)
		{
			unsigned iSzBin = hsize_1 / unsigned(_uNs);
			unsigned iIdx = hsize_1 + (iSzBin / 2);

			for (unsigned i = 0; i<_uNs; ++i)
			{
				indexes[i] = iIdx;
				iIdx += iSzBin;
			}
		}
		else
		{
			iota(indexes.begin(), indexes.end(), hsize_1);
		}


		for (uint ii = 0; ii<minPoints; ++ii)
		{
			uint i = indexes[ii];

			float x1 = float(e1[i].x);
			float y1 = float(e1[i].y);

			uint begin = 0;
			uint end = size_2 - 1;

			float xb = float(e2[begin].x);
			float yb = float(e2[begin].y);
			float res_begin = ((xb - x1) * dy_ref) - ((yb - y1) * dx_ref);
			int sign_begin = sgn(res_begin);
			if (sign_begin == 0)
			{
				//found
				med.push_back(Point2f((xb + x1)* 0.5f, (yb + y1)* 0.5f));
				continue;
			}

			float xe = float(e2[end].x);
			float ye = float(e2[end].y);
			float res_end = ((xe - x1) * dy_ref) - ((ye - y1) * dx_ref);
			int sign_end = sgn(res_end);
			if (sign_end == 0)
			{
				//found
				med.push_back(Point2f((xe + x1)* 0.5f, (ye + y1)* 0.5f));
				continue;
			}

			if ((sign_begin + sign_end) != 0)
			{
				continue;
			}

			uint j = (begin + end) >> 1;

			while (end - begin > 2)
			{
				float x2 = float(e2[j].x);
				float y2 = float(e2[j].y);
				float res = ((x2 - x1) * dy_ref) - ((y2 - y1) * dx_ref);
				int sign_res = sgn(res);

				if (sign_res == 0)
				{
					//found
					med.push_back(Point2f((x2 + x1)* 0.5f, (y2 + y1)* 0.5f));
					break;
				}

				if (sign_res + sign_begin == 0)
				{
					sign_end = sign_res;
					end = j;
				}
				else
				{
					sign_begin = sign_res;
					begin = j;
				}
				j = (begin + end) >> 1;
			}

			med.push_back(Point2f((e2[j].x + x1)* 0.5f, (e2[j].y + y1)* 0.5f));
		}
		
		if (med.size() < 2)
		{
			data.isValid = false;
			return;
		}
		q4 = GetMedianSlope(med, M34, data.Sb);
	}

	if (q2 == q4)
	{
		data.isValid = false;
		return;
	}

	float invDen = 1 / (q2 - q4);
	data.Cab.x = (M34.y - q4*M34.x - M12.y + q2*M12.x) * invDen;
	data.Cab.y = (q2*M34.y - q4*M12.y + q2*q4*(M12.x - M34.x)) * invDen;
	data.ta = q2;
	data.tb = q4;
	data.Ma = M12;
	data.Mb = M34;
};




void CEllipseDetectorYaed::DetectEdges13(cv::Mat& DP, VVP& points_1, VVP& points_3)
{
	// Vector of connected edge points
	VVP contours;

	// Labeling 8-connected edge points, discarding edge too small
	Labeling(DP, contours, _iMinEdgeLength);
	int iContoursSize = int(contours.size());

	// For each edge
	for (int i = 0; i < iContoursSize; ++i)
	{
		VP& edgeSegment = contours[i];

#ifndef DISCARD_CONSTRAINT_OBOX

		// Selection strategy - Step 1 - See Sect [3.1.2] of the paper
		// Constraint on axes aspect ratio
		RotatedRect oriented = minAreaRect(edgeSegment);
		float o_min = min(oriented.size.width, oriented.size.height);

		if (o_min < _fMinOrientedRectSide)
		{
			continue;
		}
#endif

		// Order edge points of the same arc
		sort(edgeSegment.begin(), edgeSegment.end(), SortTopLeft2BottomRight);
		int iEdgeSegmentSize = unsigned(edgeSegment.size());

		// Get extrema of the arc
		Point& left = edgeSegment[0];
		Point& right = edgeSegment[iEdgeSegmentSize - 1];

		// Find convexity - See Sect [3.1.3] of the paper
		int iCountTop = 0;
		int xx = left.x;
		for (int k = 1; k < iEdgeSegmentSize; ++k)
		{
			if (edgeSegment[k].x == xx) continue;

			iCountTop += (edgeSegment[k].y - left.y);
			xx = edgeSegment[k].x;
		}

		int width = abs(right.x - left.x) + 1;
		int height = abs(right.y - left.y) + 1;
		int iCountBottom = (width * height) - iEdgeSegmentSize - iCountTop;

		if (iCountBottom > iCountTop)
		{	//1
			points_1.push_back(edgeSegment);
		}
		else if (iCountBottom < iCountTop)
		{	//3
			points_3.push_back(edgeSegment);
		}
	}
};


void CEllipseDetectorYaed::DetectEdges24(cv::Mat& DN, VVP& points_2, VVP& points_4 )
{
	// Vector of connected edge points
	VVP contours;

	/// Labeling 8-connected edge points, discarding edge too small
	Labeling(DN, contours, _iMinEdgeLength);

	int iContoursSize = unsigned(contours.size());

	// For each edge
	for (int i = 0; i < iContoursSize; ++i)
	{
		VP& edgeSegment = contours[i];


#ifndef DISCARD_CONSTRAINT_OBOX

		// Selection strategy - Step 1 - See Sect [3.1.2] of the paper
		// Constraint on axes aspect ratio
		RotatedRect oriented = minAreaRect(edgeSegment);
		float o_min = min(oriented.size.width, oriented.size.height);

		if (o_min < _fMinOrientedRectSide)
		{
			continue;
		}

#endif

		// Order edge points of the same arc
		sort(edgeSegment.begin(), edgeSegment.end(), SortBottomLeft2TopRight);
		int iEdgeSegmentSize = unsigned(edgeSegment.size());

		// Get extrema of the arc
		Point& left = edgeSegment[0];
		Point& right = edgeSegment[iEdgeSegmentSize - 1];

		// Find convexity - See Sect [3.1.3] of the paper
		int iCountBottom = 0;
		int xx = left.x;
		for (int k = 1; k < iEdgeSegmentSize; ++k)
		{
			if (edgeSegment[k].x == xx) continue;

			iCountBottom += (left.y - edgeSegment[k].y);
			xx = edgeSegment[k].x;
		}

		int width = abs(right.x - left.x) + 1;
		int height = abs(right.y - left.y) + 1;
		int iCountTop = (width *height) - iEdgeSegmentSize - iCountBottom;

		if (iCountBottom > iCountTop)
		{
			//2
			points_2.push_back(edgeSegment);
		}
		else if (iCountBottom < iCountTop)
		{
			//4
			points_4.push_back(edgeSegment);
		}
	}
};

// Most important function for detecting ellipses. See Sect[3.2.3] of the paper
void CEllipseDetectorYaed::FindEllipses(	Point2f& center,
											VP& edge_i,
											VP& edge_j,
											VP& edge_k,
											EllipseData& data_ij,
											EllipseData& data_ik,
											vector<Ellipse>& ellipses
										)
{
	// Find ellipse parameters

	// 0-initialize accumulators
	memset(accN, 0, sizeof(int)*ACC_N_SIZE);
	memset(accR, 0, sizeof(int)*ACC_R_SIZE);
	memset(accA, 0, sizeof(int)*ACC_A_SIZE);

	Tac(3); //estimation

	// Get size of the 4 vectors of slopes (2 pairs of arcs)
	int sz_ij1 = int(data_ij.Sa.size());
	int sz_ij2 = int(data_ij.Sb.size());
	int sz_ik1 = int(data_ik.Sa.size());
	int sz_ik2 = int(data_ik.Sb.size());

	// Get the size of the 3 arcs
	size_t sz_ei = edge_i.size();
	size_t sz_ej = edge_j.size();
	size_t sz_ek = edge_k.size();

	// Center of the estimated ellipse
	float a0 = center.x;
	float b0 = center.y;


	// Estimation of remaining parameters
	// Uses 4 combinations of parameters. See Table 1 and Sect [3.2.3] of the paper.
	{
		float q1 = data_ij.ra;
		float q3 = data_ik.ra;
		float q5 = data_ik.rb;

		for (int ij1 = 0; ij1 < sz_ij1; ++ij1)
		{
			float q2 = data_ij.Sa[ij1];

			float q1xq2 = q1*q2;

			for (int ik1 = 0; ik1 < sz_ik1; ++ik1)
			{
				float q4 = data_ik.Sa[ik1];

				float q3xq4 = q3*q4;

				// See Eq. [13-18] in the paper

				float a = (q1xq2 - q3xq4);
				float b = (q3xq4 + 1)*(q1 + q2) - (q1xq2 + 1)*(q3 + q4);
				float Kp = (-b + sqrt(b*b + 4 * a*a)) / (2 * a);
				float zplus = ((q1 - Kp)*(q2 - Kp)) / ((1 + q1*Kp)*(1 + q2*Kp));

				if (zplus >= 0.0f)
				{
					continue;
				}

				float Np = sqrt(-zplus);
				float rho = atan(Kp);
				int rhoDeg;
				if (Np > 1.f)
				{
					Np = 1.f / Np;
					rhoDeg = cvRound((rho * 180 / CV_PI) + 180) % 180; // [0,180)					
				}
				else
				{
					rhoDeg = cvRound((rho * 180 / CV_PI) + 90) % 180; // [0,180)
				}

				int iNp = cvRound(Np * 100); // [0, 100]

				if (0 <= iNp	&& iNp < ACC_N_SIZE &&
					0 <= rhoDeg	&& rhoDeg < ACC_R_SIZE
					)
				{
					++accN[iNp];	// Increment N accumulator
					++accR[rhoDeg];	// Increment R accumulator
				}
			}


			for (int ik2 = 0; ik2 < sz_ik2; ++ik2)
			{
				float q4 = data_ik.Sb[ik2];

				float q5xq4 = q5*q4;

				// See Eq. [13-18] in the paper

				float a = (q1xq2 - q5xq4);
				float b = (q5xq4 + 1)*(q1 + q2) - (q1xq2 + 1)*(q5 + q4);
				float Kp = (-b + sqrt(b*b + 4 * a*a)) / (2 * a);
				float zplus = ((q1 - Kp)*(q2 - Kp)) / ((1 + q1*Kp)*(1 + q2*Kp));

				if (zplus >= 0.0f)
				{
					continue;
				}

				float Np = sqrt(-zplus);
				float rho = atan(Kp);
				int rhoDeg;
				if (Np > 1.f)
				{
					Np = 1.f / Np;
					rhoDeg = cvRound((rho * 180 / CV_PI) + 180) % 180; // [0,180)					
				}
				else
				{
					rhoDeg = cvRound((rho * 180 / CV_PI) + 90) % 180; // [0,180)
				}

				int iNp = cvRound(Np * 100); // [0, 100]

				if (0 <= iNp	&& iNp < ACC_N_SIZE &&
					0 <= rhoDeg	&& rhoDeg < ACC_R_SIZE
					)
				{
					++accN[iNp];		// Increment N accumulator
					++accR[rhoDeg];		// Increment R accumulator
				}
			}

		}
	}


	{
		float q1 = data_ij.rb;
		float q3 = data_ik.rb;
		float q5 = data_ik.ra;

		for (int ij2 = 0; ij2 < sz_ij2; ++ij2)
		{
			float q2 = data_ij.Sb[ij2];

			float q1xq2 = q1*q2;

			for (int ik2 = 0; ik2 < sz_ik2; ++ik2)
			{
				float q4 = data_ik.Sb[ik2];

				float q3xq4 = q3*q4;

				// See Eq. [13-18] in the paper

				float a = (q1xq2 - q3xq4);
				float b = (q3xq4 + 1)*(q1 + q2) - (q1xq2 + 1)*(q3 + q4);
				float Kp = (-b + sqrt(b*b + 4 * a*a)) / (2 * a);
				float zplus = ((q1 - Kp)*(q2 - Kp)) / ((1 + q1*Kp)*(1 + q2*Kp));

				if (zplus >= 0.0f)
				{
					continue;
				}

				float Np = sqrt(-zplus);
				float rho = atan(Kp);
				int rhoDeg;
				if (Np > 1.f)
				{
					Np = 1.f / Np;
					rhoDeg = cvRound((rho * 180 / CV_PI) + 180) % 180; // [0,180)
				}
				else
				{
					rhoDeg = cvRound((rho * 180 / CV_PI) + 90) % 180; // [0,180)
				}

				int iNp = cvRound(Np * 100); // [0, 100]

				if (0 <= iNp	&& iNp < ACC_N_SIZE &&
					0 <= rhoDeg	&& rhoDeg < ACC_R_SIZE
					)
				{
					++accN[iNp];		// Increment N accumulator
					++accR[rhoDeg];		// Increment R accumulator
				}
			}


			for (int ik1 = 0; ik1 < sz_ik1; ++ik1)
			{
				float q4 = data_ik.Sa[ik1];

				float q5xq4 = q5*q4;

				// See Eq. [13-18] in the paper

				float a = (q1xq2 - q5xq4);
				float b = (q5xq4 + 1)*(q1 + q2) - (q1xq2 + 1)*(q5 + q4);
				float Kp = (-b + sqrt(b*b + 4 * a*a)) / (2 * a);
				float zplus = ((q1 - Kp)*(q2 - Kp)) / ((1 + q1*Kp)*(1 + q2*Kp));

				if (zplus >= 0.0f)
				{
					continue;
				}

				float Np = sqrt(-zplus);
				float rho = atan(Kp);
				int rhoDeg;
				if (Np > 1.f)
				{
					Np = 1.f / Np;
					rhoDeg = cvRound((rho * 180 / CV_PI) + 180) % 180; // [0,180)
				}
				else
				{
					rhoDeg = cvRound((rho * 180 / CV_PI) + 90) % 180; // [0,180)
				}

				int iNp = cvRound(Np * 100); // [0, 100]

				if (0 <= iNp	&& iNp < ACC_N_SIZE &&
					0 <= rhoDeg	&& rhoDeg < ACC_R_SIZE
					)
				{
					++accN[iNp];		// Increment N accumulator
					++accR[rhoDeg];		// Increment R accumulator
				}
			}

		}
	}

	// Find peak in N and K accumulator
	int iN = FindMaxN(accN);
	int iK = FindMaxK(accR);

	// Recover real values
	float fK = float(iK);
	float Np = float(iN) * 0.01f;
	float rho = fK * float(CV_PI) / 180.f;	//deg 2 rad
	float Kp = tan(rho);

	// Estimate A. See Eq. [19 - 22] in Sect [3.2.3] of the paper

	for (ushort l = 0; l < sz_ei; ++l)
	{
		Point& pp = edge_i[l];
		float sk = 1.f / sqrt(Kp*Kp + 1.f);
		float x0 = ((pp.x - a0) * sk) + (((pp.y - b0)*Kp) * sk);
		float y0 = -(((pp.x - a0) * Kp) * sk) + ((pp.y - b0) * sk);
		float Ax = sqrt((x0*x0*Np*Np + y0*y0) / ((Np*Np)*(1.f + Kp*Kp)));
		int A = cvRound(abs(Ax / cos(rho)));
		if ((0 <= A) && (A < ACC_A_SIZE))
		{
			++accA[A];
		}
	}

	for (ushort l = 0; l < sz_ej; ++l)
	{
		Point& pp = edge_j[l];
		float sk = 1.f / sqrt(Kp*Kp + 1.f);
		float x0 = ((pp.x - a0) * sk) + (((pp.y - b0)*Kp) * sk);
		float y0 = -(((pp.x - a0) * Kp) * sk) + ((pp.y - b0) * sk);
		float Ax = sqrt((x0*x0*Np*Np + y0*y0) / ((Np*Np)*(1.f + Kp*Kp)));
		int A = cvRound(abs(Ax / cos(rho)));
		if ((0 <= A) && (A < ACC_A_SIZE))
		{
			++accA[A];
		}
	}

	for (ushort l = 0; l < sz_ek; ++l)
	{
		Point& pp = edge_k[l];
		float sk = 1.f / sqrt(Kp*Kp + 1.f);
		float x0 = ((pp.x - a0) * sk) + (((pp.y - b0)*Kp) * sk);
		float y0 = -(((pp.x - a0) * Kp) * sk) + ((pp.y - b0) * sk);
		float Ax = sqrt((x0*x0*Np*Np + y0*y0) / ((Np*Np)*(1.f + Kp*Kp)));
		int A = cvRound(abs(Ax / cos(rho)));
		if ((0 <= A) && (A < ACC_A_SIZE))
		{
			++accA[A];
		}
	}

	// Find peak in A accumulator
	int A = FindMaxA(accA);
	float fA = float(A);

	// Find B value. See Eq [23] in the paper
	float fB = abs(fA * Np);

	// Got all ellipse parameters!
	Ellipse ell(a0, b0, fA, fB, fmod(rho + float(CV_PI)*2.f, float(CV_PI)));

	Toc(3); //estimation
	Tac(4); //validation

	// Get the score. See Sect [3.3.1] in the paper

	// Find the number of edge pixel lying on the ellipse
	float _cos = cos(-ell._rad);
	float _sin = sin(-ell._rad);

	float invA2 = 1.f / (ell._a * ell._a);
	float invB2 = 1.f / (ell._b * ell._b);

	float invNofPoints = 1.f / float(sz_ei + sz_ej + sz_ek);
	int counter_on_perimeter = 0;

	for (ushort l = 0; l < sz_ei; ++l)
	{
		float tx = float(edge_i[l].x) - ell._xc;
		float ty = float(edge_i[l].y) - ell._yc;
		float rx = (tx*_cos - ty*_sin);
		float ry = (tx*_sin + ty*_cos);

		float h = (rx*rx)*invA2 + (ry*ry)*invB2;
		if (abs(h - 1.f) < _fDistanceToEllipseContour)
		{
			++counter_on_perimeter;
		}
	}

	for (ushort l = 0; l < sz_ej; ++l)
	{
		float tx = float(edge_j[l].x) - ell._xc;
		float ty = float(edge_j[l].y) - ell._yc;
		float rx = (tx*_cos - ty*_sin);
		float ry = (tx*_sin + ty*_cos);

		float h = (rx*rx)*invA2 + (ry*ry)*invB2;
		if (abs(h - 1.f) < _fDistanceToEllipseContour)
		{
			++counter_on_perimeter;
		}
	}

	for (ushort l = 0; l < sz_ek; ++l)
	{
		float tx = float(edge_k[l].x) - ell._xc;
		float ty = float(edge_k[l].y) - ell._yc;
		float rx = (tx*_cos - ty*_sin);
		float ry = (tx*_sin + ty*_cos);

		float h = (rx*rx)*invA2 + (ry*ry)*invB2;
		if (abs(h - 1.f) < _fDistanceToEllipseContour)
		{
			++counter_on_perimeter;
		}
	}

	//no points found on the ellipse
	if (counter_on_perimeter <= 0)
	{
		Toc(4); //validation
		return;
	}

	// Compute score
	float score = float(counter_on_perimeter) * invNofPoints;
	if (score < _fMinScore)
	{
		Toc(4); //validation
		return;
	}

	// Compute reliability	
	// this metric is not described in the paper, mostly due to space limitations.
	// The main idea is that for a given ellipse (TD) even if the score is high, the arcs 
	// can cover only a small amount of the contour of the estimated ellipse. 
	// A low reliability indicate that the arcs form an elliptic shape by chance, but do not underlie
	// an actual ellipse. The value is normalized between 0 and 1. 
	// The default value is 0.4.

	// It is somehow similar to the "Angular Circumreference Ratio" saliency criteria 
	// as in the paper: 
	// D. K. Prasad, M. K. Leung, S.-Y. Cho, Edge curvature and convexity
	// based ellipse detection method, Pattern Recognition 45 (2012) 3204-3221.

	float di, dj, dk;
	{
		Point2f p1(float(edge_i[0].x), float(edge_i[0].y));
		Point2f p2(float(edge_i[sz_ei - 1].x), float(edge_i[sz_ei - 1].y));
		p1.x -= ell._xc;
		p1.y -= ell._yc;
		p2.x -= ell._xc;
		p2.y -= ell._yc;
		Point2f r1((p1.x*_cos - p1.y*_sin), (p1.x*_sin + p1.y*_cos));
		Point2f r2((p2.x*_cos - p2.y*_sin), (p2.x*_sin + p2.y*_cos));
		di = abs(r2.x - r1.x) + abs(r2.y - r1.y);
	}
	{
		Point2f p1(float(edge_j[0].x), float(edge_j[0].y));
		Point2f p2(float(edge_j[sz_ej - 1].x), float(edge_j[sz_ej - 1].y));
		p1.x -= ell._xc;
		p1.y -= ell._yc;
		p2.x -= ell._xc;
		p2.y -= ell._yc;
		Point2f r1((p1.x*_cos - p1.y*_sin), (p1.x*_sin + p1.y*_cos));
		Point2f r2((p2.x*_cos - p2.y*_sin), (p2.x*_sin + p2.y*_cos));
		dj = abs(r2.x - r1.x) + abs(r2.y - r1.y);
	}
	{
		Point2f p1(float(edge_k[0].x), float(edge_k[0].y));
		Point2f p2(float(edge_k[sz_ek - 1].x), float(edge_k[sz_ek - 1].y));
		p1.x -= ell._xc;
		p1.y -= ell._yc;
		p2.x -= ell._xc;
		p2.y -= ell._yc;
		Point2f r1((p1.x*_cos - p1.y*_sin), (p1.x*_sin + p1.y*_cos));
		Point2f r2((p2.x*_cos - p2.y*_sin), (p2.x*_sin + p2.y*_cos));
		dk = abs(r2.x - r1.x) + abs(r2.y - r1.y);
	}

	// This allows to get rid of thick edges
	float rel = min(1.f, ((di + dj + dk) / (3 * (ell._a + ell._b))));

	if (rel < _fMinReliability)
	{
		Toc(4); //validation
		return;
	}

	// Assign the new score!
	ell._score = (score + rel) * 0.5f;
	//ell._score = score;

	// The tentative detection has been confirmed. Save it!
	ellipses.push_back(ell);

	Toc(4); // Validation
};

// Get the coordinates of the center, given the intersection of the estimated lines. See Fig. [8] in Sect [3.2.3] in the paper.
Point2f CEllipseDetectorYaed::GetCenterCoordinates(EllipseData& data_ij, EllipseData& data_ik)
{
	float xx[7];
	float yy[7];

	xx[0] = data_ij.Cab.x;
	xx[1] = data_ik.Cab.x;
	yy[0] = data_ij.Cab.y;
	yy[1] = data_ik.Cab.y;

	{
		//1-1
		float q2 = data_ij.ta;
		float q4 = data_ik.ta;
		Point2f& M12 = data_ij.Ma;
		Point2f& M34 = data_ik.Ma;

		float invDen = 1 / (q2 - q4);
		xx[2] = (M34.y - q4*M34.x - M12.y + q2*M12.x) * invDen;
		yy[2] = (q2*M34.y - q4*M12.y + q2*q4*(M12.x - M34.x)) * invDen;
	}

	{
		//1-2
		float q2 = data_ij.ta;
		float q4 = data_ik.tb;
		Point2f& M12 = data_ij.Ma;
		Point2f& M34 = data_ik.Mb;

		float invDen = 1 / (q2 - q4);
		xx[3] = (M34.y - q4*M34.x - M12.y + q2*M12.x) * invDen;
		yy[3] = (q2*M34.y - q4*M12.y + q2*q4*(M12.x - M34.x)) * invDen;
	}

	{
		//2-2
		float q2 = data_ij.tb;
		float q4 = data_ik.tb;
		Point2f& M12 = data_ij.Mb;
		Point2f& M34 = data_ik.Mb;

		float invDen = 1 / (q2 - q4);
		xx[4] = (M34.y - q4*M34.x - M12.y + q2*M12.x) * invDen;
		yy[4] = (q2*M34.y - q4*M12.y + q2*q4*(M12.x - M34.x)) * invDen;
	}

	{
		//2-1
		float q2 = data_ij.tb;
		float q4 = data_ik.ta;
		Point2f& M12 = data_ij.Mb;
		Point2f& M34 = data_ik.Ma;

		float invDen = 1 / (q2 - q4);
		xx[5] = (M34.y - q4*M34.x - M12.y + q2*M12.x) * invDen;
		yy[5] = (q2*M34.y - q4*M12.y + q2*q4*(M12.x - M34.x)) * invDen;
	}

	xx[6] = (xx[0] + xx[1]) * 0.5f;
	yy[6] = (yy[0] + yy[1]) * 0.5f;


	// Median
	nth_element(xx, xx + 3, xx + 7);
	nth_element(yy, yy + 3, yy + 7);
	float xc = xx[3];
	float yc = yy[3];

	return Point2f(xc, yc);
};


// Verify triplets of arcs with convexity: i=1, j=2, k=4
void CEllipseDetectorYaed::Triplets124(VVP& pi,
	VVP& pj,
	VVP& pk,
	unordered_map<uint, EllipseData>& data,
	vector<Ellipse>& ellipses
	)
{
	// get arcs length
	ushort sz_i = ushort(pi.size());
	ushort sz_j = ushort(pj.size());
	ushort sz_k = ushort(pk.size());

	// For each edge i
	for (ushort i = 0; i < sz_i; ++i)
	{
		VP& edge_i = pi[i];
		ushort sz_ei = ushort(edge_i.size());

		Point& pif = edge_i[0];
		Point& pil = edge_i[sz_ei - 1];

		// 1,2 -> reverse 1, swap
		VP rev_i(edge_i.size());
		reverse_copy(edge_i.begin(), edge_i.end(), rev_i.begin());

		// For each edge j
		for (ushort j = 0; j < sz_j; ++j)
		{
			VP& edge_j = pj[j];
			ushort sz_ej = ushort(edge_j.size());

			Point& pjf = edge_j[0];
			Point& pjl = edge_j[sz_ej - 1];

#ifndef DISCARD_CONSTRAINT_POSITION
			// CONSTRAINTS on position
			if (pjl.x > pif.x + _fThPosition) //is right	
			{
				//discard
				continue;
			}
#endif

			uint key_ij = GenerateKey(PAIR_12, i, j);

			//for each edge k
			for (ushort k = 0; k < sz_k; ++k)
			{
				VP& edge_k = pk[k];
				ushort sz_ek = ushort(edge_k.size());

				Point& pkf = edge_k[0];
				Point& pkl = edge_k[sz_ek - 1];

#ifndef DISCARD_CONSTRAINT_POSITION
				//CONSTRAINTS on position
				if (pkl.y < pil.y - _fThPosition)
				{
					//discard
					continue;
				}
#endif

				uint key_ik = GenerateKey(PAIR_14, i, k);

				// Find centers

				EllipseData data_ij, data_ik;

				// If the data for the pair i-j have not been computed yet
				if (data.count(key_ij) == 0)
				{
					//1,2 -> reverse 1, swap

					// Compute data!
					GetFastCenter(edge_j, rev_i, data_ij);
					// Insert computed data in the hash table
					data.insert(pair<uint, EllipseData>(key_ij, data_ij));
				}
				else
				{
					// Otherwise, just lookup the data in the hash table
					data_ij = data.at(key_ij);
				}

				// If the data for the pair i-k have not been computed yet
				if (data.count(key_ik) == 0)
				{
					//1,4 -> ok

					// Compute data!
					GetFastCenter(edge_i, edge_k, data_ik);
					// Insert computed data in the hash table
					data.insert(pair<uint, EllipseData>(key_ik, data_ik));
				}
				else
				{
					// Otherwise, just lookup the data in the hash table
					data_ik = data.at(key_ik);
				}

				// INVALID CENTERS
				if (!data_ij.isValid || !data_ik.isValid)
				{
					continue;
				}

#ifndef DISCARD_CONSTRAINT_CENTER
				// Selection strategy - Step 3. See Sect [3.2.2] in the paper
				// The computed centers are not close enough
				if (ed2(data_ij.Cab, data_ik.Cab) > _fMaxCenterDistance2)
				{
					//discard
					continue;
				}
#endif
				// If all constraints of the selection strategy have been satisfied, 
				// we can start estimating the ellipse parameters

				// Find ellipse parameters

				// Get the coordinates of the center (xc, yc)
				Point2f center = GetCenterCoordinates(data_ij, data_ik);

				// Find remaining paramters (A,B,rho)
				FindEllipses(center, edge_i, edge_j, edge_k, data_ij, data_ik, ellipses);
			}
		}
	}
};



void CEllipseDetectorYaed::Triplets231(VVP& pi,
	VVP& pj,
	VVP& pk,
	unordered_map<uint, EllipseData>& data,
	vector<Ellipse>& ellipses
	)
{
	ushort sz_i = ushort(pi.size());
	ushort sz_j = ushort(pj.size());
	ushort sz_k = ushort(pk.size());

	// For each edge i
	for (ushort i = 0; i < sz_i; ++i)
	{
		VP& edge_i = pi[i];
		ushort sz_ei = ushort(edge_i.size());

		Point& pif = edge_i[0];
		Point& pil = edge_i[sz_ei - 1];

		VP rev_i(edge_i.size());
		reverse_copy(edge_i.begin(), edge_i.end(), rev_i.begin());

		// For each edge j
		for (ushort j = 0; j < sz_j; ++j)
		{
			VP& edge_j = pj[j];
			ushort sz_ej = ushort(edge_j.size());

			Point& pjf = edge_j[0];
			Point& pjl = edge_j[sz_ej - 1];

#ifndef DISCARD_CONSTRAINT_POSITION
			// CONSTRAINTS on position
			if (pjf.y < pif.y - _fThPosition)
			{
				//discard
				continue;
			}
#endif

			VP rev_j(edge_j.size());
			reverse_copy(edge_j.begin(), edge_j.end(), rev_j.begin());

			uint key_ij = GenerateKey(PAIR_23, i, j);

			// For each edge k
			for (ushort k = 0; k < sz_k; ++k)
			{
				VP& edge_k = pk[k];
				ushort sz_ek = ushort(edge_k.size());

				Point& pkf = edge_k[0];
				Point& pkl = edge_k[sz_ek - 1];

#ifndef DISCARD_CONSTRAINT_POSITION
				// CONSTRAINTS on position
				if (pkf.x < pil.x - _fThPosition)
				{
					//discard
					continue;
				}
#endif
				uint key_ik = GenerateKey(PAIR_12, k, i);

				// Find centers

				EllipseData data_ij, data_ik;

				if (data.count(key_ij) == 0)
				{
					// 2,3 -> reverse 2,3

					GetFastCenter(rev_i, rev_j, data_ij);
					data.insert(pair<uint, EllipseData>(key_ij, data_ij));
				}
				else
				{
					data_ij = data.at(key_ij);
				}

				if (data.count(key_ik) == 0)
				{
					// 2,1 -> reverse 1
					VP rev_k(edge_k.size());
					reverse_copy(edge_k.begin(), edge_k.end(), rev_k.begin());

					GetFastCenter(edge_i, rev_k, data_ik);
					data.insert(pair<uint, EllipseData>(key_ik, data_ik));
				}
				else
				{
					data_ik = data.at(key_ik);
				}

				// INVALID CENTERS
				if (!data_ij.isValid || !data_ik.isValid)
				{
					continue;
				}

#ifndef DISCARD_CONSTRAINT_CENTER
				// CONSTRAINT ON CENTERS
				if (ed2(data_ij.Cab, data_ik.Cab) > _fMaxCenterDistance2)
				{
					//discard
					continue;
				}
#endif
				// Find ellipse parameters
				Point2f center = GetCenterCoordinates(data_ij, data_ik);

				FindEllipses(center, edge_i, edge_j, edge_k, data_ij, data_ik, ellipses);

			}
		}
	}
};


void CEllipseDetectorYaed::Triplets342(VVP& pi,
	VVP& pj,
	VVP& pk,
	unordered_map<uint, EllipseData>& data,
	vector<Ellipse>& ellipses
	)
{
	ushort sz_i = ushort(pi.size());
	ushort sz_j = ushort(pj.size());
	ushort sz_k = ushort(pk.size());

	// For each edge i
	for (ushort i = 0; i < sz_i; ++i)
	{
		VP& edge_i = pi[i];
		ushort sz_ei = ushort(edge_i.size());

		Point& pif = edge_i[0];
		Point& pil = edge_i[sz_ei - 1];

		VP rev_i(edge_i.size());
		reverse_copy(edge_i.begin(), edge_i.end(), rev_i.begin());

		// For each edge j
		for (ushort j = 0; j < sz_j; ++j)
		{
			VP& edge_j = pj[j];
			ushort sz_ej = ushort(edge_j.size());

			Point& pjf = edge_j[0];
			Point& pjl = edge_j[sz_ej - 1];

#ifndef DISCARD_CONSTRAINT_POSITION
			//CONSTRAINTS on position
			if (pjf.x < pil.x - _fThPosition) 		//is left
			{
				//discard
				continue;
			}
#endif

			VP rev_j(edge_j.size());
			reverse_copy(edge_j.begin(), edge_j.end(), rev_j.begin());

			uint key_ij = GenerateKey(PAIR_34, i, j);

			// For each edge k
			for (ushort k = 0; k < sz_k; ++k)
			{
				VP& edge_k = pk[k];
				ushort sz_ek = ushort(edge_k.size());

				Point& pkf = edge_k[0];
				Point& pkl = edge_k[sz_ek - 1];

#ifndef DISCARD_CONSTRAINT_POSITION
				//CONSTRAINTS on position
				if (pkf.y > pif.y + _fThPosition)
				{
					//discard
					continue;
				}
#endif
				uint key_ik = GenerateKey(PAIR_23, k, i);

				// Find centers

				EllipseData data_ij, data_ik;

				if (data.count(key_ij) == 0)
				{
					//3,4 -> reverse 4

					GetFastCenter(edge_i, rev_j, data_ij);
					data.insert(pair<uint, EllipseData>(key_ij, data_ij));
				}
				else
				{
					data_ij = data.at(key_ij);
				}

				if (data.count(key_ik) == 0)
				{
					//3,2 -> reverse 3,2

					VP rev_k(edge_k.size());
					reverse_copy(edge_k.begin(), edge_k.end(), rev_k.begin());

					GetFastCenter(rev_i, rev_k, data_ik);

					data.insert(pair<uint, EllipseData>(key_ik, data_ik));
				}
				else
				{
					data_ik = data.at(key_ik);
				}


				// INVALID CENTERS
				if (!data_ij.isValid || !data_ik.isValid)
				{
					continue;
				}

#ifndef DISCARD_CONSTRAINT_CENTER
				// CONSTRAINT ON CENTERS
				if (ed2(data_ij.Cab, data_ik.Cab) > _fMaxCenterDistance2)
				{
					//discard
					continue;
				}
#endif
				// Find ellipse parameters
				Point2f center = GetCenterCoordinates(data_ij, data_ik);
				FindEllipses(center, edge_i, edge_j, edge_k, data_ij, data_ik, ellipses);
			}
		}

	}
};



void CEllipseDetectorYaed::Triplets413(VVP& pi,
	VVP& pj,
	VVP& pk,
	unordered_map<uint, EllipseData>& data,
	vector<Ellipse>& ellipses
	)
{
		ushort sz_i = ushort(pi.size());
		ushort sz_j = ushort(pj.size());
		ushort sz_k = ushort(pk.size());

		// For each edge i
		for (ushort i = 0; i < sz_i; ++i)
		{
			VP& edge_i = pi[i];
			ushort sz_ei = ushort(edge_i.size());

			Point& pif = edge_i[0];
			Point& pil = edge_i[sz_ei - 1];

			VP rev_i(edge_i.size());
			reverse_copy(edge_i.begin(), edge_i.end(), rev_i.begin());

			// For each edge j
			for (ushort j = 0; j < sz_j; ++j)
			{
				VP& edge_j = pj[j];
				ushort sz_ej = ushort(edge_j.size());

				Point& pjf = edge_j[0];
				Point& pjl = edge_j[sz_ej - 1];

#ifndef DISCARD_CONSTRAINT_POSITION
				//CONSTRAINTS on position
				if (pjl.y > pil.y + _fThPosition)  		//is below
				{
					//discard
					continue;
				}
#endif

				uint key_ij = GenerateKey(PAIR_14, j, i);

				// For each edge k
				for (ushort k = 0; k < sz_k; ++k)
				{
					VP& edge_k = pk[k];
					ushort sz_ek = ushort(edge_k.size());

					Point& pkf = edge_k[0];
					Point& pkl = edge_k[sz_ek - 1];

#ifndef DISCARD_CONSTRAINT_POSITION
					//CONSTRAINTS on position
					if (pkl.x > pif.x + _fThPosition)
					{
						//discard
						continue;
					}
#endif
					uint key_ik = GenerateKey(PAIR_34, k, i);

					// Find centers

					EllipseData data_ij, data_ik;

					if (data.count(key_ij) == 0)
					{
						// 4,1 -> OK
						GetFastCenter(edge_i, edge_j, data_ij);
						data.insert(pair<uint, EllipseData>(key_ij, data_ij));
					}
					else
					{
						data_ij = data.at(key_ij);
					}

					if (data.count(key_ik) == 0)
					{
						// 4,3 -> reverse 4
						GetFastCenter(rev_i, edge_k, data_ik);
						data.insert(pair<uint, EllipseData>(key_ik, data_ik));
					}
					else
					{
						data_ik = data.at(key_ik);
					}

					// INVALID CENTERS
					if (!data_ij.isValid || !data_ik.isValid)
					{
						continue;
					}

#ifndef DISCARD_CONSTRAINT_CENTER
					// CONSTRAIN ON CENTERS
					if (ed2(data_ij.Cab, data_ik.Cab) > _fMaxCenterDistance2)
					{
						//discard
						continue;
					}
#endif
					// Find ellipse parameters
					Point2f center = GetCenterCoordinates(data_ij, data_ik);

					FindEllipses(center, edge_i, edge_j, edge_k, data_ij, data_ik, ellipses);

				}
			}
		}
	};


void CEllipseDetectorYaed::RemoveShortEdges(cv::Mat& edges, cv::Mat& clean)
{
	VVP contours;

	// Labeling and contraints on length
	Labeling(edges, contours, _iMinEdgeLength);

	int iContoursSize = (int)contours.size();
	for (int i = 0; i < iContoursSize; ++i)
	{
		VP& edge = contours[i];
		unsigned szEdge = (unsigned)edge.size();

		// Constraint on axes aspect ratio
		RotatedRect oriented = minAreaRect(edge);
		if (oriented.size.width < _fMinOrientedRectSide ||
			oriented.size.height < _fMinOrientedRectSide ||
			oriented.size.width > oriented.size.height * _fMaxRectAxesRatio ||
			oriented.size.height > oriented.size.width * _fMaxRectAxesRatio)
		{
			continue;
		}

		for (unsigned j = 0; j < szEdge; ++j)
		{
			clean.at<uchar>(edge[j]) = (uchar)255;
		}
	}
}



void CEllipseDetectorYaed::PreProcessing(cv::Mat& I,
	cv::Mat& DP,
	cv::Mat& DN
	)
{

	Tic(0); //edge detection

	// Smooth image
	GaussianBlur(I, I, _szPreProcessingGaussKernelSize, _dPreProcessingGaussSigma);

	// Temp variables
	Mat1b E;				//edge mask
	Mat1s DX, DY;			//sobel derivatives

	// Detect edges
	Canny3(I, E, DX, DY, 3, false);

	Toc(0); //edge detection

	Tac(1); //preprocessing

	// For each edge points, compute the edge direction
	for (int i = 0; i<_szImg.height; ++i)
	{
		short* _dx = DX.ptr<short>(i);
		short* _dy = DY.ptr<short>(i);
		uchar* _e = E.ptr<uchar>(i);
		uchar* _dp = DP.ptr<uchar>(i);
		uchar* _dn = DN.ptr<uchar>(i);

		for (int j = 0; j<_szImg.width; ++j)
		{
			if (!((_e[j] <= 0) || (_dx[j] == 0) || (_dy[j] == 0)))
			{
				// Angle of the tangent
				float phi = -(float(_dx[j]) / float(_dy[j]));

				// Along positive or negative diagonal
				if (phi > 0)	_dp[j] = (uchar)255;
				else if (phi < 0)	_dn[j] = (uchar)255;
			}
		}
	}
};


void CEllipseDetectorYaed::DetectAfterPreProcessing(vector<Ellipse>& ellipses, cv::Mat& E, Mat1f& PHI)
{
	// Set the image size
	_szImg = E.size();

	// Initialize temporary data structures
	cv::Mat DP = cv::Mat::zeros(_szImg, E.type());		// arcs along positive diagonal
	cv::Mat DN = cv::Mat::zeros(_szImg, E.type());		// arcs along negative diagonal

	// For each edge points, compute the edge direction
	for (int i = 0; i<_szImg.height; ++i)
	{
		float* _phi = PHI.ptr<float>(i);
		uchar* _e = E.ptr<uchar>(i);
		uchar* _dp = DP.ptr<uchar>(i);
		uchar* _dn = DN.ptr<uchar>(i);

		for (int j = 0; j<_szImg.width; ++j)
		{
			if ((_e[j] > 0) && (_phi[j] != 0))
			{
				// Angle
				
				// along positive or negative diagonal
				if (_phi[j] > 0)	_dp[j] = (uchar)255;
				else if (_phi[j] < 0)	_dn[j] = (uchar)255;
			}
		}
	}

	// Initialize accumulator dimensions
	ACC_N_SIZE = 101;
	ACC_R_SIZE = 180;
	ACC_A_SIZE = max(_szImg.height, _szImg.width);

	// Allocate accumulators
	accN = new int[ACC_N_SIZE];
	accR = new int[ACC_R_SIZE];
	accA = new int[ACC_A_SIZE];

	// Other temporary 
	VVP points_1, points_2, points_3, points_4;		//vector of points, one for each convexity class
	unordered_map<uint, EllipseData> centers;		//hash map for reusing already computed EllipseData

	// Detect edges and find convexities
	DetectEdges13(DP, points_1, points_3);
	DetectEdges24(DN, points_2, points_4);

	// Find triplets
	Triplets124(points_1, points_2, points_4, centers, ellipses);
	Triplets231(points_2, points_3, points_1, centers, ellipses);
	Triplets342(points_3, points_4, points_2, centers, ellipses);
	Triplets413(points_4, points_1, points_3, centers, ellipses);

	// Sort detected ellipses with respect to score
	sort(ellipses.begin(), ellipses.end());

	//free accumulator memory
	delete[] accN;
	delete[] accR;
	delete[] accA;

	//cluster detections
	//ClusterEllipses(ellipses);
};


void CEllipseDetectorYaed::Detect(cv::Mat& I, vector<Ellipse>& ellipses)
{
	golf_sim::LoggingTools::DebugShowImage("CEllipseDetectorYaed::Detect - Input Image I", I);

	Tic(1); //prepare data structure

	// Set the image size
	_szImg = I.size();

	// Initialize temporary data structures
	/*
	cv::Mat DP = cv::Mat::zeros(_szImg, CV_8S);		// arcs along positive diagonal
	cv::Mat DN = cv::Mat::zeros(_szImg, CV_8S);		// arcs along negative diagonal
	*/
	Mat1b DP = Mat1b::zeros(_szImg);		// arcs along positive diagonal
	Mat1b DN = Mat1b::zeros(_szImg);		// arcs along negative diagonal

	// Initialize accumulator dimensions
	ACC_N_SIZE = 101;
	ACC_R_SIZE = 180;
	ACC_A_SIZE = max(_szImg.height, _szImg.width);

	// Allocate accumulators
	accN = new int[ACC_N_SIZE];
	accR = new int[ACC_R_SIZE];
	accA = new int[ACC_A_SIZE];

	// Other temporary 
	VVP points_1, points_2, points_3, points_4;		//vector of points, one for each convexity class
	unordered_map<uint, EllipseData> centers;		//hash map for reusing already computed EllipseData

	Toc(1); //prepare data structure

	// Preprocessing
	// From input image I, find edge point with coarse convexity along positive (DP) or negative (DN) diagonal
	PreProcessing(I, DP, DN);

	// Detect edges and find convexities
	DetectEdges13(DP, points_1, points_3);
	DetectEdges24(DN, points_2, points_4);

	// golf_sim::LoggingTools::DebugShowImage("DP", DP);
	// golf_sim::LoggingTools::DebugShowImage("DN", DN);

	Toc(1); //preprocessing


	// DEBUG
	Mat3b out(I.rows, I.cols, Vec3b(0,0,0));
	for(unsigned i=0; i<points_1.size(); ++i)
	{
		//Vec3b color(rand()%255, 128+rand()%127, 128+rand()%127);
		Vec3b color(255,0,0);
		for(unsigned j=0; j<points_1[i].size(); ++j)
			out(points_1[i][j]) = color;
	}

	for(unsigned i=0; i<points_2.size(); ++i)
	{
		//Vec3b color(rand()%255, 128+rand()%127, 128+rand()%127);
		Vec3b color(0,255,0);
		for(unsigned j=0; j<points_2[i].size(); ++j)
			out(points_2[i][j]) = color;
	}
	for(unsigned i=0; i<points_3.size(); ++i)
	{
		//Vec3b color(rand()%255, 128+rand()%127, 128+rand()%127);
		Vec3b color(0,0,255);
		for(unsigned j=0; j<points_3[i].size(); ++j)
			out(points_3[i][j]) = color;
	}

	for(unsigned i=0; i<points_4.size(); ++i)
	{
		//Vec3b color(rand()%255, 128+rand()%127, 128+rand()%127);
		Vec3b color(255,0,255);
		for(unsigned j=0; j<points_4[i].size(); ++j)
			out(points_4[i][j]) = color;
	}

	golf_sim::LoggingTools::DebugShowImage("out", out);

	// time estimation, validation  inside

	Tic(2); //grouping
	//find triplets
	Triplets124(points_1, points_2, points_4, centers, ellipses);
	Triplets231(points_2, points_3, points_1, centers, ellipses);
	Triplets342(points_3, points_4, points_2, centers, ellipses);
	Triplets413(points_4, points_1, points_3, centers, ellipses);
	Toc(2); //grouping	
	// time estimation, validation inside
	_times[2] -= (_times[3] + _times[4]);

	Tac(4); //validation
	// Sort detected ellipses with respect to score
	sort(ellipses.begin(), ellipses.end());
	Toc(4); //validation

	// Free accumulator memory
	delete[] accN;
	delete[] accR;
	delete[] accA;

	Tic(5);
	// Cluster detections
	ClusterEllipses(ellipses);
	Toc(5);

};




// Ellipse clustering procedure. See Sect [3.3.2] in the paper.
void CEllipseDetectorYaed::ClusterEllipses(vector<Ellipse>& ellipses)
{
	float th_Da = 0.1f;
	float th_Db = 0.1f;
	float th_Dr = 0.1f;

	float th_Dc_ratio = 0.1f;
	float th_Dr_circle = 0.9f;

	int iNumOfEllipses = int(ellipses.size());
	if (iNumOfEllipses == 0) return;

	// The first ellipse is assigned to a cluster
	vector<Ellipse> clusters;
	clusters.push_back(ellipses[0]);

	bool bFoundCluster = false;

	for (int i = 1; i<iNumOfEllipses; ++i)
	{
		Ellipse& e1 = ellipses[i];

		int sz_clusters = int(clusters.size());

		float ba_e1 = e1._b / e1._a;
		float Decc1 = e1._b / e1._a;

		bool bFoundCluster = false;
		for (int j = 0; j<sz_clusters; ++j)
		{
			Ellipse& e2 = clusters[j];

			float ba_e2 = e2._b / e2._a;
			float th_Dc = min(e1._b, e2._b) * th_Dc_ratio;
			th_Dc *= th_Dc;

			// Centers
			float Dc = ((e1._xc - e2._xc)*(e1._xc - e2._xc) + (e1._yc - e2._yc)*(e1._yc - e2._yc));
			if (Dc > th_Dc)
			{
				//not same cluster
				continue;
			}

			// a
			float Da = abs(e1._a - e2._a) / max(e1._a, e2._a);
			if (Da > th_Da)
			{
				//not same cluster
				continue;
			}

			// b
			float Db = abs(e1._b - e2._b) / min(e1._b, e2._b);
			if (Db > th_Db)
			{
				//not same cluster
				continue;
			}

			// angle
			float Dr = GetMinAnglePI(e1._rad, e2._rad) / float(CV_PI);
			if ((Dr > th_Dr) && (ba_e1 < th_Dr_circle) && (ba_e2 < th_Dr_circle))
			{
				//not same cluster
				continue;
			}

			// Same cluster as e2
			bFoundCluster = true;
			// Discard, no need to create a new cluster
			break;
		}

		if (!bFoundCluster)
		{
			// Create a new cluster			
			clusters.push_back(e1);
		}
	}

	clusters.swap(ellipses);
};



//Draw at most iTopN detected ellipses.
void CEllipseDetectorYaed::DrawDetectedEllipses(Mat3b& output, vector<Ellipse>& ellipses, int iTopN, int thickness)
{
	int sz_ell = int(ellipses.size());
	int n = (iTopN == 0) ? sz_ell : min(iTopN, sz_ell);
	for (int i = 0; i < n; ++i)
	{
		Ellipse& e = ellipses[n - i - 1];
		int g = cvRound(e._score * 255.f);
		Scalar color(0, g, 0);
		ellipse(output, Point(cvRound(e._xc), cvRound(e._yc)), Size(cvRound(e._a), cvRound(e._b)), e._rad*180.0 / CV_PI, 0.0, 360.0, color, thickness);
	}
}

}
