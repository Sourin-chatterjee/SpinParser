/**
 * @file XYZMeasurementCorrelation.cpp
 * @author Finn Lasse Buessen
 * @brief Correlation measurement for models with diagonal interactions.
 * 
 * @copyright Copyright (c) 2020
 */

#define _USE_MATH_DEFINES
#include <math.h>
#include <hdf5.h>
#include "lib/Integrator.hpp"
#include "lib/ValueBundle.hpp"
#include "XYZMeasurementCorrelation.hpp"
#include "SpinParser.hpp"
#include "XYZFrgCore.hpp"
#include "XYZEffectiveAction.hpp"

XYZMeasurementCorrelation::XYZMeasurementCorrelation(const std::string &outfile, const float minCutoff, const float maxCutoff, const bool defer) : Measurement(outfile, minCutoff, maxCutoff, defer, true)
{
	_currentCutoff = -1.0f;
	int latticeSizeExtended = 0;
	for (auto i = FrgCommon::lattice().getRange(0); i != FrgCommon::lattice().end(); ++i) ++latticeSizeExtended;
	int latticeSizeBasis = int(FrgCommon::lattice()._basis.size());
	_memoryStepLattice = latticeSizeBasis * latticeSizeExtended;

	//prepare correlation buffer
	_correlationsXX = new float[latticeSizeBasis * latticeSizeExtended];
	_correlationsYY = new float[latticeSizeBasis * latticeSizeExtended];
	_correlationsZZ = new float[latticeSizeBasis * latticeSizeExtended];
	_correlationsDD = new float[latticeSizeBasis * latticeSizeExtended];

	//set up loadManager
	//stack0
	HMP::StackIdentifier dataStack0 = SpinParser::spinParser()->getLoadManager()->addMasterStackExplicit<float>(
		&_currentCutoff,
		1,
		[](int linearIterator)->float { return SpinParser::spinParser()->getFrgCore()->flowingFunctional()->cutoff; },
		1,
		1,
		true
	);
	//stack1
	HMP::StackIdentifier dataStack1 = SpinParser::spinParser()->getLoadManager()->addMasterStackImplicit<float>(
		_correlationsXX,
		1,
		std::bind(&XYZMeasurementCorrelation::_calculateCorrelation, this, std::placeholders::_1),
		latticeSizeBasis * latticeSizeExtended,
		1,
		1,
		false
	);
	//stack2
	SpinParser::spinParser()->getLoadManager()->addSlaveStack<float>(
		_correlationsYY,
		1,
		dataStack1,
		latticeSizeBasis * latticeSizeExtended
	);
	//stack3
	SpinParser::spinParser()->getLoadManager()->addSlaveStack<float>(
		_correlationsZZ,
		1,
		dataStack1,
		latticeSizeBasis * latticeSizeExtended
	);
	//stack4
	SpinParser::spinParser()->getLoadManager()->addSlaveStack<float>(
		_correlationsDD,
		1,
		dataStack1,
		latticeSizeBasis * latticeSizeExtended
	);
	_loadManagedStacks.insert(_loadManagedStacks.end(), { dataStack0, dataStack1 });
}

XYZMeasurementCorrelation::~XYZMeasurementCorrelation()
{
	delete[] _correlationsXX;
	delete[] _correlationsYY;
	delete[] _correlationsZZ;
	delete[] _correlationsDD;
}

void XYZMeasurementCorrelation::takeMeasurement(const EffectiveAction &state, const bool isMasterTask) const
{
	if (_currentCutoff != state.cutoff) SpinParser::spinParser()->getLoadManager()->calculate(_loadManagedStacks.data(), int(_loadManagedStacks.size()));

	if (isMasterTask)
	{
		_writeOutfileCorrelation("XYZCorXX", _correlationsXX);
		_writeOutfileCorrelation("XYZCorYY", _correlationsYY);
		_writeOutfileCorrelation("XYZCorZZ", _correlationsZZ);
		_writeOutfileCorrelation("XYZCorDD", _correlationsDD);
	}
}

void XYZMeasurementCorrelation::_calculateCorrelation(const int iterator) const
{
	//calculate real space susceptibility
	float nu = 0.0f;
	float cut = SpinParser::spinParser()->getFrgCore()->flowingFunctional()->cutoff;
	XYZVertexSingleParticle *v2 = static_cast<XYZEffectiveAction *>(SpinParser::spinParser()->getFrgCore()->flowingFunctional())->vertexSingleParticle;
	XYZVertexTwoParticle *v4 = static_cast<XYZEffectiveAction *>(SpinParser::spinParser()->getFrgCore()->flowingFunctional())->vertexTwoParticle;

	ValueSuperbundle<float, 4> susceptibility(FrgCommon::lattice().size);
	ValueSuperbundle<float, 4> stackBuffer(FrgCommon::lattice().size);
	ValueSuperbundle<float, 4> buffer1(FrgCommon::lattice().size);
	ValueSuperbundle<float, 4> buffer2(FrgCommon::lattice().size);
	ValueSuperbundle<float, 4> buffer3(FrgCommon::lattice().size);
	ValueSuperbundle<float, 4> buffer4(FrgCommon::lattice().size);

	//integration kernel
	std::function<void(float, ValueSuperbundle<float, 4> &)> integralKernel = [&](const float w, ValueSuperbundle<float, 4> &returnBuffer) -> void
	{
		returnBuffer.reset();

		//term1
		float term1 = 1.0f / ((w + v2->getValue(w)) * (w + nu + v2->getValue(w + nu)));
		returnBuffer.bundle(static_cast<int>(SpinComponent::X))[0] += term1 / float(4.0f * M_PI);
		returnBuffer.bundle(static_cast<int>(SpinComponent::Y))[0] += term1 / float(4.0f * M_PI);
		returnBuffer.bundle(static_cast<int>(SpinComponent::Z))[0] += term1 / float(4.0f * M_PI);
		returnBuffer.bundle(static_cast<int>(SpinComponent::None))[0] += term1 / float(M_PI);

		//term2
		std::function<void(float, ValueSuperbundle<float, 4> &)> innerKernel = [&](const float wp, ValueSuperbundle<float, 4> &ret) -> void
		{
			ret.reset();
			const XYZVertexTwoParticleAccessBuffer<8> ab0 = v4->generateAccessBuffer(w + wp + nu, nu, w - wp);
			v4->getValueSuperbundle(ab0, stackBuffer);

			const float vx = v4->getValue(FrgCommon::lattice().zero(), FrgCommon::lattice().zero(), w + wp + nu, w - wp, nu, SpinComponent::X, XYZVertexTwoParticle::FrequencyChannel::None);
			const float vy = v4->getValue(FrgCommon::lattice().zero(), FrgCommon::lattice().zero(), w + wp + nu, w - wp, nu, SpinComponent::Y, XYZVertexTwoParticle::FrequencyChannel::None);
			const float vz = v4->getValue(FrgCommon::lattice().zero(), FrgCommon::lattice().zero(), w + wp + nu, w - wp, nu, SpinComponent::Z, XYZVertexTwoParticle::FrequencyChannel::None);
			const float vd = v4->getValue(FrgCommon::lattice().zero(), FrgCommon::lattice().zero(), w + wp + nu, w - wp, nu, SpinComponent::None, XYZVertexTwoParticle::FrequencyChannel::None);

			//dumbbell diagram
			ret.bundle(static_cast<int>(SpinComponent::X)).multSub(1.0f, stackBuffer.bundle(static_cast<int>(SpinComponent::X)));
			ret.bundle(static_cast<int>(SpinComponent::Y)).multSub(1.0f, stackBuffer.bundle(static_cast<int>(SpinComponent::Y)));
			ret.bundle(static_cast<int>(SpinComponent::Z)).multSub(1.0f, stackBuffer.bundle(static_cast<int>(SpinComponent::Z)));
			ret.bundle(static_cast<int>(SpinComponent::None)).multSub(4.0f, stackBuffer.bundle(static_cast<int>(SpinComponent::None)));

			//egg diagram
			ret.bundle(static_cast<int>(SpinComponent::X))[0] += 0.5f * (vx - vy - vz + vd);
			ret.bundle(static_cast<int>(SpinComponent::Y))[0] += 0.5f * (-vx + vy - vz + vd);
			ret.bundle(static_cast<int>(SpinComponent::Z))[0] += 0.5f * (-vx - vy + vz + vd);
			ret.bundle(static_cast<int>(SpinComponent::None))[0] += 2.0f * (vx + vy + vz + vd);

			float normalization = 1.0f / ((w + v2->getValue(w)) * (w + nu + v2->getValue(w + nu)) * (wp + v2->getValue(wp)) * (wp + nu + v2->getValue(wp + nu)) * float(4.0f * M_PI * M_PI));
			ret *= normalization;
		};
		if (-(nu + cut) > *FrgCommon::frequency().beginNegative())
		{
			ImplicitIntegrator::integrateWithObscureRightBoundary(FrgCommon::frequency().beginNegative(), -cut - nu, innerKernel, buffer3, buffer4);
			returnBuffer += buffer4;
		}
		if (nu - cut > cut)
		{
			ImplicitIntegrator::integrateWithObscureBoundaries(-nu + cut, -cut, innerKernel, buffer3, buffer4);
			returnBuffer += buffer4;
		}
		if (cut < *FrgCommon::frequency().last())
		{
			ImplicitIntegrator::integrateWithObscureLeftBoundary(cut, FrgCommon::frequency().last(), innerKernel, buffer3, buffer4);
			returnBuffer += buffer4;
		}
	};

	if (-(nu + cut) > *FrgCommon::frequency().beginNegative())
	{
		ImplicitIntegrator::integrateWithObscureRightBoundary(FrgCommon::frequency().beginNegative(), -cut - nu, integralKernel, buffer1, buffer2);
		susceptibility += buffer2;
	}
	if (nu - cut > cut)
	{
		ImplicitIntegrator::integrateWithObscureBoundaries(-nu + cut, -cut, integralKernel, buffer1, buffer2);
		susceptibility += buffer2;
	}
	if (cut < *FrgCommon::frequency().last())
	{
		ImplicitIntegrator::integrateWithObscureLeftBoundary(cut, FrgCommon::frequency().last(), integralKernel, buffer1, buffer2);
		susceptibility += buffer2;
	}

	int offset = iterator * _memoryStepLattice;
	for (auto i = FrgCommon::lattice().getBasis(); i != FrgCommon::lattice().end(); ++i)
	{
		for (auto j = FrgCommon::lattice().getRange(i); j != FrgCommon::lattice().end(); ++j)
		{
			SpinComponent component;
			int rid;
            float sign =1.0f;
			component = SpinComponent::X;
			rid = FrgCommon::lattice().symmetryTransform(i, j, component,sign);
			_correlationsXX[offset] = susceptibility.bundle(static_cast<int>(component))[rid];

			component = SpinComponent::Y;
			rid = FrgCommon::lattice().symmetryTransform(i, j, component,sign);
			_correlationsYY[offset] = susceptibility.bundle(static_cast<int>(component))[rid];

			component = SpinComponent::Z;
			rid = FrgCommon::lattice().symmetryTransform(i, j, component,sign);
			_correlationsZZ[offset] = susceptibility.bundle(static_cast<int>(component))[rid];

			component = SpinComponent::None;
			rid = FrgCommon::lattice().symmetryTransform(i, j, component,sign);
			_correlationsDD[offset] = susceptibility.bundle(static_cast<int>(component))[rid];

			++offset;
		}
	}
}

void XYZMeasurementCorrelation::_writeOutfileHeader(const std::string &observableGroup) const
{
	H5Eset_auto(H5E_DEFAULT, NULL, NULL);

	//open file
	hid_t file = H5Fopen(outfile().c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
	if (file < 0) throw Exception(Exception::Type::IOError, "Could not open observable file [" + outfile() + "] for writing.");

	//open group
	hid_t group = H5Gopen(file, observableGroup.c_str(), H5P_DEFAULT);
	if (group < 0) throw Exception(Exception::Type::IOError, "Could not open obsfile group [" + observableGroup + "] for writing. ");

	//create meta group
	if (H5Lexists(group, "meta", H5P_DEFAULT) == 0)
	{
		hid_t mgroup = H5Gcreate(group, "meta", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

		const int dataTypeLatticeSiteDim = 1;
		const hsize_t dataTypeLatticeSiteSize[dataTypeLatticeSiteDim] = { 3 };
		hid_t dataTypeLatticeSite = H5Tarray_create(H5T_NATIVE_FLOAT, dataTypeLatticeSiteDim, dataTypeLatticeSiteSize);

		//write lattice vectors
		float *latticeBuffer = new float[3 * 3];
		int i = 0;
		for (auto a = FrgCommon::lattice()._bravaisLattice.begin(); a != FrgCommon::lattice()._bravaisLattice.end(); ++a)
		{
			latticeBuffer[3 * i] = float(a->x);
			latticeBuffer[3 * i + 1] = float(a->y);
			latticeBuffer[3 * i + 2] = float(a->z);
			++i;
		}
		const int attrSpaceDimLattice = 1;
		const hsize_t attrSpaceSizeLattice[attrSpaceDimLattice] = { FrgCommon::lattice()._bravaisLattice.size() };
		hid_t attrSpaceLattice = H5Screate_simple(attrSpaceDimLattice, attrSpaceSizeLattice, NULL);
		hid_t datasetLattice = H5Dcreate(mgroup, "latticeVectors", dataTypeLatticeSite, attrSpaceLattice, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Dwrite(datasetLattice, dataTypeLatticeSite, H5S_ALL, H5S_ALL, H5P_DEFAULT, latticeBuffer);
		H5Dclose(datasetLattice);
		H5Sclose(attrSpaceLattice);
		delete[] latticeBuffer;

		//write basis
		float *basisBuffer = new float[3 * FrgCommon::lattice()._basis.size()];
		i = 0;
		for (auto b = FrgCommon::lattice().getBasis(); b != FrgCommon::lattice().end(); ++b)
		{
			basisBuffer[3 * i] = float(FrgCommon::lattice().getSitePosition(b).x);
			basisBuffer[3 * i + 1] = float(FrgCommon::lattice().getSitePosition(b).y);
			basisBuffer[3 * i + 2] = float(FrgCommon::lattice().getSitePosition(b).z);
			++i;
		}
		const int attrSpaceDimBasis = 1;
		const hsize_t attrSpaceSizeBasis[attrSpaceDimBasis] = { FrgCommon::lattice()._basis.size() };
		hid_t attrSpaceBasis = H5Screate_simple(attrSpaceDimBasis, attrSpaceSizeBasis, NULL);
		hid_t datasetBasis = H5Dcreate(mgroup, "basis", dataTypeLatticeSite, attrSpaceBasis, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Dwrite(datasetBasis, dataTypeLatticeSite, H5S_ALL, H5S_ALL, H5P_DEFAULT, basisBuffer);
		H5Dclose(datasetBasis);
		H5Sclose(attrSpaceBasis);
		delete[] basisBuffer;

		//write sites
		int inRangeCount = 0;
		for (SublatticeIterator i = FrgCommon::lattice().getRange(0); i != FrgCommon::lattice().end(); ++i) ++inRangeCount;

		const int dataSpaceDimSitesReference = 2;
		const hsize_t dataSpaceSizeSitesReference[dataSpaceDimSitesReference] = { FrgCommon::lattice()._basis.size(), hsize_t(inRangeCount) };
		hid_t dataSpaceSitesReference = H5Screate_simple(dataSpaceDimSitesReference, dataSpaceSizeSitesReference, NULL);

		float *SitesReferenceBuffer = new float[FrgCommon::lattice()._basis.size() * inRangeCount * 3];
		int j = 0;
		for (unsigned int b = 0; b < FrgCommon::lattice()._basis.size(); ++b)
		{
			for (SublatticeIterator i = FrgCommon::lattice().getRange(b); i != FrgCommon::lattice().end(); ++i)
			{
				*(SitesReferenceBuffer + 3 * j) = (float)FrgCommon::lattice().getSitePosition(i).x;
				*(SitesReferenceBuffer + 3 * j + 1) = (float)FrgCommon::lattice().getSitePosition(i).y;
				*(SitesReferenceBuffer + 3 * j + 2) = (float)FrgCommon::lattice().getSitePosition(i).z;
				++j;
			}
		}
		hid_t datasetSitesReference = H5Dcreate(mgroup, "sites", dataTypeLatticeSite, dataSpaceSitesReference, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Dwrite(datasetSitesReference, dataTypeLatticeSite, H5S_ALL, H5S_ALL, H5P_DEFAULT, SitesReferenceBuffer);
		H5Dclose(datasetSitesReference);
		H5Sclose(dataSpaceSitesReference);
		delete[] SitesReferenceBuffer;

		//close meta group
		H5Tclose(dataTypeLatticeSite);
		H5Gclose(mgroup);
	}
	else
	{
		Log::log << Log::LogLevel::Warning << "The observable output file [" + outfile() + "] already contains the group [" + observableGroup + "/meta]. Skipping writing this information. " << Log::endl;
	}


	//close file
	H5Gclose(group);
	H5Fclose(file);
}

void XYZMeasurementCorrelation::_writeOutfileCorrelation(const std::string &observableGroup, const float *correlation) const
{
	H5Eset_auto(H5E_DEFAULT, NULL, NULL);

	//open or create file
	hid_t file = (H5Fis_hdf5(outfile().c_str()) > 0) ? H5Fopen(outfile().c_str(), H5F_ACC_RDWR, H5P_DEFAULT) : H5Fcreate(outfile().c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if (file < 0) throw Exception(Exception::Type::IOError, "Could not open observable file [" + outfile() + "] for writing");

	//open or create group
	hid_t group = (H5Lexists(file, observableGroup.c_str(), H5P_DEFAULT) > 0) ? H5Gopen(file, observableGroup.c_str(), H5P_DEFAULT) : H5Gcreate(file, observableGroup.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (group < 0) throw Exception(Exception::Type::IOError, "Could not open obsfile group [" + observableGroup + "] for writing");

	//ensure that meta information is included
	if (H5Lexists(group, "meta", H5P_DEFAULT) == 0) _writeOutfileHeader(observableGroup);

	//open or create data collection
	hid_t data = (H5Lexists(group, "data", H5P_DEFAULT) > 0) ? H5Gopen(group, "data", H5P_DEFAULT) : H5Gcreate(group, "data", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (data < 0) throw Exception(Exception::Type::IOError, "Could not open obsfile group [" + observableGroup + "/data] for writing");

	//determine unique dataset name and check for duplicate dataset
	int datasetId = 0;
	hsize_t numDatasets;
	H5Gget_num_objs(data, &numDatasets);
	for (int i = 0; i < int(numDatasets); ++i)
	{
		if (H5Gget_objtype_by_idx(data, i) == H5G_GROUP)
		{
			++datasetId;

			const int datasetNameMaxLength = 32;
			char datasetName[datasetNameMaxLength];
			H5Gget_objname_by_idx(data, i, datasetName, datasetNameMaxLength);

			hid_t measurement = H5Gopen(data, datasetName, H5P_DEFAULT);
			hid_t attr = H5Aopen(measurement, "cutoff", H5P_DEFAULT);
			float c;
			H5Aread(attr, H5T_NATIVE_FLOAT, &c);
			H5Aclose(attr);
			H5Gclose(measurement);

			if (c == _currentCutoff)
			{
				Log::log << Log::LogLevel::Warning << "Found existing correlation measurement at cutoff " + std::to_string(_currentCutoff) + ". Discarding duplicate entry." << Log::endl;
				return;
			}
		}
	}
	std::string datasetName = "measurement_" + std::to_string(datasetId);

	//create new measurement group
	hid_t measurement = H5Gcreate(data, datasetName.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	const int attrSpaceDim = 1;
	const hsize_t attrSpaceSize[1] = { 1 };
	hid_t attrSpace = H5Screate_simple(attrSpaceDim, attrSpaceSize, NULL);
	hid_t attr = H5Acreate(measurement, "cutoff", H5T_NATIVE_FLOAT, attrSpace, H5P_DEFAULT, H5P_DEFAULT);
	H5Awrite(attr, H5T_NATIVE_FLOAT, &_currentCutoff);
	H5Aclose(attr);
	H5Sclose(attrSpace);

	//write data
	int inRangeCount = 0;
	for (SublatticeIterator i = FrgCommon::lattice().getRange(0); i != FrgCommon::lattice().end(); ++i) ++inRangeCount;
	const int dataSpaceDim = 2;
	const hsize_t dataSpaceSize[2] = { FrgCommon::lattice()._basis.size(), hsize_t(inRangeCount) };
	hid_t dataSpace = H5Screate_simple(dataSpaceDim, dataSpaceSize, NULL);

	hid_t dataset = H5Dcreate(measurement, "data", H5T_NATIVE_FLOAT, dataSpace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	H5Dwrite(dataset, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, correlation);
	H5Dclose(dataset);

	H5Sclose(dataSpace);

	//clean up
	H5Gclose(measurement);
	H5Gclose(data);
	H5Gclose(group);
	H5Fclose(file);
}