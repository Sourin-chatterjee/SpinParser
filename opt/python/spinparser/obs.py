import warnings
import h5py
import numpy as np

def __getObsAttributes(obsIdentifier):
	"""
	For a given obsfile identifier, return a list of all contained datasets. 

	Args:
		obsIdentifier (str): obsfile identifier (hdf5 dataset name).

	Returns:
		list[str]: List of data contained on the obsfile identifier. 
	"""
	if (obsIdentifier == "TRICorDD"):
		return [ "CorrelationDD", "LatticeData" ]
	elif (obsIdentifier == "TRICorXX"):
		return [ "CorrelationXX", "LatticeData" ]
	elif (obsIdentifier == "TRICorXY"):
		return [ "CorrelationXY", "LatticeData" ]
	elif (obsIdentifier == "TRICorXZ"):
		return [ "CorrelationXZ", "LatticeData" ]
	elif (obsIdentifier == "TRICorYX"):
		return [ "CorrelationYX", "LatticeData" ]
	elif (obsIdentifier == "TRICorYY"):
		return [ "CorrelationYY", "LatticeData" ]
	elif (obsIdentifier == "TRICorYZ"):
		return [ "CorrelationYZ", "LatticeData" ]
	elif (obsIdentifier == "TRICorZX"):
		return [ "CorrelationZX", "LatticeData" ]
	elif (obsIdentifier == "TRICorZY"):
		return [ "CorrelationZY", "LatticeData" ]
	elif (obsIdentifier == "TRICorZZ"):
		return [ "CorrelationZZ", "LatticeData" ]
	elif (obsIdentifier == "XYZCorDD"):
		return [ "CorrelationDD", "LatticeData" ]
	elif (obsIdentifier == "XYZCorXX"):
		return [ "CorrelationXX", "LatticeData" ]
	elif (obsIdentifier == "XYZCorYY"):
		return [ "CorrelationYY", "LatticeData" ]
	elif (obsIdentifier == "XYZCorZZ"):
		return [ "CorrelationZZ", "LatticeData" ]
	elif (obsIdentifier == "SU2CorDD"):
		return [ "CorrelationDD", "LatticeData" ]
	elif (obsIdentifier == "SU2CorZZ"):
		return [ "CorrelationXX", "CorrelationYY", "CorrelationZZ", "LatticeData" ]
	else:
		raise Exception("Unknown observable identifier %s" % obsIdentifier)

def getLatticeBasis(obsfile):
	"""
	Return the lattice basis for the lattice graph used in the calculation of the specified obsfile. 

	Args:
		obsfile (str): Path to the obsfile. 

	Returns:
		numpy.ndarray: Array of coordinates of lattice basis sites. For an N-site basis, the return array has shape (N,3), with the second dimension storing the x, y, and z components of the position vector.
	"""
	with h5py.File(obsfile, "r") as file:
		for obsIdentifier in file.keys():
			if "LatticeData" in __getObsAttributes(obsIdentifier):
				return file[obsIdentifier+"/meta/basis"][()]
	raise Exception("Observable file does not contain lattice basis information.")

def getLatticePrimitives(obsfile):
	"""
	Return the primitive lattice vectors for the lattice graph used in the calculation of the specified obsfile. 

	Args:
		obsfile (str): Path to the obsfile. 

	Returns:
		numpy.ndarray: Array of primitive lattice vectors. The return array has shape (3,3), with the first dimension enumerating the primitives, and the second dimension storing the x, y, and z components of the primitive.
	"""
	with h5py.File(obsfile, "r") as file:
		for obsIdentifier in file.keys():
			if ("LatticeData" in __getObsAttributes(obsIdentifier)):
				return file[obsIdentifier+"/meta/latticeVectors"][()]
	raise Exception("Observable file does not contain primitive lattice vector information.")

def getCutoffValues(obsfile, identifier, verbose=True):
	with h5py.File(obsfile, "r") as file:
		try:
			measurements = list(file[identifier+"/data"].keys())
			measurements = sorted(measurements, key=lambda x:int(x.split("_")[1]))
			cutoffs = np.array([ file[identifier+"/data/"+m].attrs["cutoff"][0] for m in measurements ])
			return {"data":cutoffs, "measurements":measurements} if verbose else cutoffs
		except:
			raise Exception("Observable file does not contain cutoff information under the given observable identifier.")

def getLatticeSites(obsfile, reference=0, verbose=True):
	"""
	Get a list of all lattice sites within truncation range of a specified reference site. 

	Args:
		obsfile (str): Path to the obsfile.
		reference (optional): Reference site. Can be either an integer `n` which refers to the n-th basis site, or a three-component list or numpy.array which specifies the real-space position of a basis site. Defaults to 0.
		verbose (bool, optional): Defines whether the output should be verbose. Defaults to True.

	Returns:
		dict|numpy.ndarray: If `verbose==False`, return an array of shape (N,3) where N is the number of sites within truncation range, and the second dimension stores the x, y, and z components of the lattice site position. 
			If the output is verbose, return a dict with keys `data`, containing the nonverbose data, and `reference`, which contains the real-space position of the specified reference site. 
	"""
	#parse argument `reference`
	if type(reference) == int and reference < len(getLatticeBasis(obsfile)): reference = getLatticeBasis(obsfile)[reference,:]
	elif type(reference) == list: reference = np.array(reference)
	if not (reference.ndim == 1 and len(reference) == 3): raise Exception("Invalid argument type: reference")

	referenceList = getLatticeBasis(obsfile)
	distanceTable = [np.linalg.norm(x-reference) for x in referenceList]
	if np.min(distanceTable) > 1e-3: raise Exception("Specified reference site does not match any basis site. Try higher precision or select from getLatticeBasis().")
	referenceFilter = np.argmin(distanceTable)

	#read data
	out = None
	with h5py.File(obsfile, "r") as file:
		for obsIdentifier in file.keys():
			if ("LatticeData" in __getObsAttributes(obsIdentifier)):
				raw = file[obsIdentifier+"/meta/sites"][()]
				out = {"data":raw[referenceFilter,:], "reference":referenceList[referenceFilter,:]}
				break
	
	#return data
	if not out: raise Exception("Observable file does not contain lattice site information.")
	return out if verbose else out["data"]

def getCorrelation(obsfile, cutoff="all", site="all", reference=0, component="all", verbose=True):
	"""
	Obtain correlation data from an observables file.

	Args:
		obsfile (str): Path to the obsfile.
		cutoff (float|list|np.array|str, optional): Cutoff values at which to retrieve data. Can be a single cutoff value, a list of cutoff values, or "all". Defaults to "all".
		site (list|np.array|str, optional): Lattice sites at which to retrieve data. Can be a list of x,y,z real-space coordinates of a site, a list of multiple sites, or "all". Defaults to "all".
		reference (int|list|np.array, optional): Reference site for the correlation measurement. Can be either an integer `n` which refers to the n-th basis site, or a three-component list or numpy.array which specifies the real-space position of a basis site. Defaults to 0. 
		component (str, optional): Spin component of the correlation function to retrieve. Can be "XX", "XY", "XZ", "YX", ..., "ZZ", or "all". The latter is equivalent to XX+YY+ZZ. Defaults to "all". 
		verbose (bool, optional): Defines whether the output should be verbose. Defaults to True.

	Returns:
		dict|numpy.ndarray: If `verbose==False`, return an array of shape (N,M) where N is the number of cutoff values selected and N is the number of sites selected.
			If the output is verbose, return a dict with keys `data` (contains the nonverbose data), `cutoff` (contains the selected cutoff values), `site` (contains the selected lattice sites), and `reference` (contains the real-space position of the specified reference site). 
	"""
	if component == "all":
		out = getCorrelation(obsfile, cutoff=cutoff, site=site, reference=reference, verbose=True, component="XX")
		out["data"] += getCorrelation(obsfile, cutoff=cutoff, site=site, reference=reference, verbose=False, component="YY")
		out["data"] += getCorrelation(obsfile, cutoff=cutoff, site=site, reference=reference, verbose=False, component="ZZ")
	else:
		out = None
		with h5py.File(obsfile, "r") as file:
			for obsIdentifier in file.keys():
				if ("Correlation"+component in __getObsAttributes(obsIdentifier)):
					#parse argument `reference`
					referenceList = getLatticeBasis(obsfile)
					if type(reference) == int and reference < len(referenceList): reference = referenceList[reference,:]
					elif type(reference) == list: reference = np.array(reference)
					if not (reference.ndim == 1 and len(reference) == 3): raise Exception("Invalid argument type: reference")

					distanceTable = [np.linalg.norm(x-reference) for x in referenceList]
					referenceFilter = int(np.argmin(distanceTable))
					if np.min(distanceTable) > 1e-3: warnings.warn("Specified reference site %s does not match any basis site. Using closest site: %s" % (reference, referenceList[referenceFilter]))


					#parse argument `site`
					siteList = getLatticeSites(obsfile, reference=referenceFilter, verbose=False)
					if type(site) == str and site == "all": site = siteList
					elif type(site) == list: site = np.array(site)
					if site.ndim == 1: site = np.reshape(site, (1,len(site)))
					if not (site.ndim == 2 and site.shape[1] == 3): raise Exception("Invalid argument type: site")

					siteFilter = []
					for i in range(len(site)):
						distanceTable = [np.linalg.norm(x-site[i]) for x in siteList]
						siteFilter.append(np.argmin(distanceTable))
						if np.min(distanceTable) > 1e-3: warnings.warn("Specified lattice site %s does not match any site in the lattice. Using closest site: %s" % (site[i], siteList[siteFilter[-1]]))

					#parse argument `cutoff`
					cutoffValues = getCutoffValues(obsfile, obsIdentifier)
					cutoffList = cutoffValues["data"]
					measurementList = cutoffValues["measurements"]
					if type(cutoff) == str and cutoff == "all": cutoff = cutoffList
					elif type(cutoff) == float: cutoff = np.array([cutoff])
					elif type(cutoff) == list: cutoff = np.array(cutoff)
					if not (cutoff.ndim == 1 and len(cutoff) > 0): raise Exception("Invalid argument type: cutoff")

					cutoffFilter = []
					cutoffFilterMeasurements = []
					for i in range(len(cutoff)):
						distanceTable = [abs(x-cutoff[i]) for x in cutoffList]
						cutoffFilter.append(np.argmin(distanceTable))
						cutoffFilterMeasurements.append(measurementList[np.argmin(distanceTable)])
						if np.min(distanceTable) > 1e-3: warnings.warn("Specified cutoff value %f does not match any recorded cutoff. Using closest value: %f" % (cutoff[i], cutoffList[cutoffFilter[-1]]))

					#read data
					data = np.ndarray((0,len(siteFilter)), dtype=np.float32)
					for i in range(len(cutoffFilter)):
						dset = file[obsIdentifier+"/data/"+cutoffFilterMeasurements[i]+"/data"][()]
						dset = np.reshape(dset[referenceFilter, siteFilter], (1,len(siteFilter)))
						data = np.concatenate((data, dset), axis=0)
					out = {"data":data, "cutoff":cutoffList[cutoffFilter], "site":siteList[siteFilter], "reference":referenceList[referenceFilter]}
					break
	#return data
	if not out: raise Exception("Observable file does not contain specificed correlation information.")
	return out if verbose else out["data"]

def getStructureFactor(obsfile, momentum, cutoff="all", component="all", verbose=True):
	"""
	Calculate the spin structure factor from the spin correlations stored in an observable file. 

	Args:
		obsfile (str): Path to the obsfile.
		momentum (list|np.array): Momentum points at which to compute the structure factor. Can be a one-dimensional list or array of three kx,ky,kz coordinates, or a two-dimensional list or array of multiple momentum points.
		cutoff (float|list|np.array|str, optional): Cutoff values at which to retrieve data. Can be a single cutoff value, a list of cutoff values, or "all". Defaults to "all".
		component (str, optional): Spin component of the structure factor to retrieve. Can be "XX", "XY", "XZ", "YX", ..., "ZZ", or "all". The latter is equivalent to XX+YY+ZZ. Defaults to "all". 
		verbose (bool, optional): Defines whether the output should be verbose. Defaults to True.

	Returns:
		dict|numpy.ndarray: If `verbose==False`, return an array of shape (N,M) where N is the number of cutoff values selected and N is the number of momentum points specified.
			If the output is verbose, return a dict with keys `data` (contains the nonverbose data), `cutoff` (contains the selected cutoff values), and `momentum` (momentum points). 
	"""
	#parse argument `momentum`
	if type(momentum) == list: momentum = np.array(momentum)
	if momentum.ndim == 1: momentum = np.reshape(momentum, (1,len(momentum)))
	if not (momentum.ndim == 2 and momentum.shape[1] == 3): raise Exception("Invalid argument type: momentum")

	#get correlation
	basis = getLatticeBasis(obsfile)
	cutoffList = getCorrelation(obsfile, cutoff=cutoff, site="all", reference=0, component=component, verbose=True)["cutoff"]

	data = np.zeros((len(cutoffList),len(momentum)), dtype=np.float32)
	for i in range(len(basis)):
		raw = getCorrelation(obsfile, cutoff=cutoff, site="all", reference=i, component=component, verbose=True)
		for i_c in range(len(raw["cutoff"])):
			for i_k in range(len(momentum)):
				for i_s in range(len(raw["site"])):
					data[i_c,i_k] += raw["data"][i_c,i_s] * np.cos(np.dot(momentum[i_k], raw["site"][i_s] - raw["reference"]))
	data /= len(basis)
	out = {"data":data, "cutoff":cutoffList, "momentum":momentum}

	#return data
	return out if verbose else out["data"]