/*
 *  beagle.cpp
 *  BEAGLE
 *
 * Copyright 2009 Phylogenetic Likelihood Working Group
 *
 * This file is part of BEAGLE.
 *
 * BEAGLE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * BEAGLE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with BEAGLE.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * @author Andrew Rambaut
 * @author Marc Suchard
 * @author Daniel Ayres
 * @author Aaron Darling
 */

#ifdef HAVE_CONFIG_H
#include "libhmsbeagle/config.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>    // for exception, bad_exception
#include <stdexcept>    // for std exception hierarchy
#include <list>
#include <utility>
#include <vector>
#include <iostream>

#include "libhmsbeagle/beagle.h"
#include "libhmsbeagle/BeagleImpl.h"

#include "libhmsbeagle/plugin/Plugin.h"

#define BEAGLE_VERSION  PACKAGE_VERSION
#define BEAGLE_CITATION "Using BEAGLE library v" PACKAGE_VERSION " for accelerated, parallel likelihood evaluation\n\
2009-2013, BEAGLE Working Group - http://beagle-lib.googlecode.com/\n\
Citation: Ayres et al (2012) Systematic Biology 61: 170-173 | doi:10.1093/sysbio/syr100\n"

typedef std::pair<int, int> IntPair;
typedef std::list<IntPair> PairedList;
typedef std::pair<int, std::pair<int, beagle::BeagleImplFactory*> >	RsrcImpl;
typedef std::list<RsrcImpl> RsrcImplList;

//#define BEAGLE_DEBUG_TIME
#ifdef BEAGLE_DEBUG_TIME
#include <sys/time.h>
double debugTimeTotal;
double debugGetTime() {
    struct timeval tim;
    gettimeofday(&tim, NULL);
    return (tim.tv_sec+(tim.tv_usec/1000000.0));
}
#define DEBUG_CREATE_TIME() debugTimeTotal=0; fprintf(stderr,"\n*** BEAGLE instance created ***\n");
#define DEBUG_START_TIME() double debugInitialTime=debugGetTime();
#define DEBUG_END_TIME() debugTimeTotal+=debugGetTime()-debugInitialTime;
#define DEBUG_FINALIZE_TIME() fprintf(stderr,"\n*** Total time used by BEAGLE instance: %f seconds ***\n", debugTimeTotal);
#else
#define DEBUG_CREATE_TIME()
#define DEBUG_START_TIME()
#define DEBUG_END_TIME()
#define DEBUG_FINALIZE_TIME()
#endif

//@CHANGED make this a std::vector<BeagleImpl *> and use at to reference.
std::vector<beagle::BeagleImpl*> *instances = NULL;

/// returns an initialized instance or NULL if the index refers to an invalid instance
namespace beagle {
BeagleImpl* getBeagleInstance(int instanceIndex);


BeagleImpl* getBeagleInstance(int instanceIndex) {
    if (instanceIndex > instances->size())
        return NULL;
    return (*instances)[instanceIndex];
}

}	// end namespace beagle

#include <Rcpp.h>
#include "beagle.h"
using namespace Rcpp;


//' @useDynLib beagleR
//' @importFrom Rcpp sourceCpp
// [[Rcpp::export]]


// A specialized comparator that only reorders based on score
bool compareRsrcImpl(const RsrcImpl &left, const RsrcImpl &right) {
	return left.first < right.first;
}

// A specialized comparator that only reorders based on first entry
bool compareOnFirst(const IntPair &left, const IntPair &right) {
	return left.first < right.first;
}

std::list<beagle::BeagleImplFactory*>* implFactory = NULL;

BeagleResourceList* rsrcList = NULL;
std::map<int, int> ResourceMap;

int loaded = 0; // Indicates is the initial library constructors have been run
                // This patches a bug with JVM under Linux that calls the finalizer twice

/** The list of plugins that provide implementations of likelihood calculators */
std::list<beagle::plugin::Plugin*>* plugins;

void beagleLoadPlugins(void) {
	if(plugins==NULL){
		plugins = new std::list<beagle::plugin::Plugin*>();
	}

	beagle::plugin::PluginManager& pm = beagle::plugin::PluginManager::instance();

	try{
		beagle::plugin::Plugin* cpuplug = pm.findPlugin("hmsbeagle-cpu");
		plugins->push_back(cpuplug);
	}catch(beagle::plugin::SharedLibraryException sle){
		// this one should always work
		std::cerr << "Unable to load CPU plugin!\n";
		std::cerr << "Please check for proper libhmsbeagle installation.\n";
	}

	try{
		beagle::plugin::Plugin* cudaplug = pm.findPlugin("hmsbeagle-cuda");
		plugins->push_back(cudaplug);
	}catch(beagle::plugin::SharedLibraryException sle){}

	try{
		beagle::plugin::Plugin* openclplug = pm.findPlugin("hmsbeagle-opencl");
		plugins->push_back(openclplug);
	}catch(beagle::plugin::SharedLibraryException sle){}

	try{
		beagle::plugin::Plugin* openclalteraplug = pm.findPlugin("hmsbeagle-opencl-altera");
		plugins->push_back(openclalteraplug);
	}catch(beagle::plugin::SharedLibraryException sle){}

	try{
		beagle::plugin::Plugin* sseplug = pm.findPlugin("hmsbeagle-cpu-sse");
		plugins->push_back(sseplug);
	}catch(beagle::plugin::SharedLibraryException sle){}

	try{
		beagle::plugin::Plugin* avxplug = pm.findPlugin("hmsbeagle-cpu-avx");
		plugins->push_back(avxplug);
	}catch(beagle::plugin::SharedLibraryException sle){}

	try{
		beagle::plugin::Plugin* openmpplug = pm.findPlugin("hmsbeagle-cpu-openmp");
		plugins->push_back(openmpplug);
	}catch(beagle::plugin::SharedLibraryException sle){}
}

std::list<beagle::BeagleImplFactory*>* beagleGetFactoryList(void) {
	if (implFactory == NULL) {
		implFactory = new std::list<beagle::BeagleImplFactory*>;
		// Set-up a list of implementation factories in trial-order
		std::list<beagle::plugin::Plugin*>::iterator plugin_iter = plugins->begin();
		for(; plugin_iter != plugins->end(); plugin_iter++ ){
			std::list<beagle::BeagleImplFactory*> factories = (*plugin_iter)->getBeagleFactories();
			implFactory->insert(implFactory->end(), factories.begin(), factories.end());
		}
	}
	return implFactory;
}

void beagle_library_initialize(void) {
//	beagleGetResourceList(); // Generate resource list at library initialization, causes Bus error on Mac
//	beagleGetFactoryList(); // Generate factory list at library initialization, causes Bus error on Mac
}

void beagle_library_finalize(void) {
  DEBUG_FINALIZE_TIME();
	// FIXME: need to destroy each plugin
	// the following code segfaults
/*	std::list<beagle::plugin::Plugin*>::iterator plugin_iter = plugins.begin();
	for(; plugin_iter != plugins.end(); plugin_iter++ ){
		delete *plugin_iter;
	}
	plugins.clear();
*/

	if(plugins!=NULL && loaded){
		delete plugins;
	}
	// Destroy implFactory.
	// The contained factory pointers will be deleted by the plugins themselves
	if (implFactory && loaded) {
		try {
		delete implFactory;
		} catch (...) {

		}
	}

	// Destroy rsrcList
	// The resources will be deleted by the plugins themselves
	if (rsrcList && loaded) {
		free(rsrcList->list);
		free(rsrcList);
	}

	// Destroy instances
	if (instances && loaded) {
		delete instances;
	}
	loaded = 0;
}

#ifdef __GNUC__
void __attribute__ ((constructor)) beagle_gnu_init(void) {
	beagle_library_initialize();
}
void __attribute__ ((destructor)) beagle_gnu_finalize(void) {
    beagle_library_finalize();
}
#endif

#ifdef _WIN32
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
		beagle_library_initialize();
        break;
    case DLL_PROCESS_DETACH:
		beagle_library_finalize();
        break;
    }
    return TRUE;
}
#endif

int beagleFinalize() {
    if (loaded)
        beagle_library_finalize();
    return BEAGLE_SUCCESS;
}

const char* beagleGetVersion() {
    return BEAGLE_VERSION;
}

const char* beagleGetCitation() {
    return BEAGLE_CITATION;
}

BeagleResourceList* beagleGetResourceList() {
	// plugins must be loaded before resources
	if (plugins==NULL)
	    beagleLoadPlugins();

    if (rsrcList == NULL) {
        // count the total resources across plugins
        rsrcList = (BeagleResourceList*) malloc(sizeof(BeagleResourceList));
        rsrcList->length = 0;
        std::list<beagle::plugin::Plugin*>::iterator plugin_iter = plugins->begin();
        for(; plugin_iter != plugins->end(); plugin_iter++ ){
            rsrcList->length += (*plugin_iter)->getBeagleResources().size();
        }

        // allocate space for a complete list of resources
        rsrcList->list = (BeagleResource*) malloc(sizeof(BeagleResource) * rsrcList->length);

        // copy in resource lists from each plugin
        int rI=0;
        for(plugin_iter = plugins->begin(); plugin_iter != plugins->end(); plugin_iter++ ){
            std::list<BeagleResource> rList = (*plugin_iter)->getBeagleResources();
            std::list<BeagleResource>::iterator r_iter = rList.begin();
            int prev_rI = rI;
            for(; r_iter != rList.end(); r_iter++){
                bool rsrcExists = false;
                for(int i=0; i<prev_rI; i++){
                    if (strcmp(rsrcList->list[i].name, r_iter->name) == 0) {
                        if (!rsrcExists) {
                            rsrcExists = true;
                            rsrcList->length--;
                        }
                        rsrcList->list[i].supportFlags |= r_iter->supportFlags;
                    }
                }

                if (!rsrcExists) {
                    ResourceMap.insert(std::pair<int, int>(rI, (rI - prev_rI)));
                    rsrcList->list[rI++] = *r_iter;
                }
            }
        }
    }
    return rsrcList;
}

int scoreFlags(long flags1, long flags2) {
    int score = 0;
    int trait = 1;
    for(int bits=0; bits<32; bits++) {
        if ( (flags1 & trait) &&
             (flags2 & trait) )
            score++;
        trait <<= 1;
    }
    return -score;
}

int beagleCreateInstance(int tipCount,
                         int partialsBufferCount,
                         int compactBufferCount,
                         int stateCount,
                         int patternCount,
                         int eigenBufferCount,
                         int matrixBufferCount,
                         int categoryCount,
                         int scaleBufferCount,
                         int* resourceList,
                         int resourceCount,
                         long preferenceFlags,
                         long requirementFlags,
                         BeagleInstanceDetails* returnInfo) {
    DEBUG_CREATE_TIME();
    try {
        if (instances == NULL)
            instances = new std::vector<beagle::BeagleImpl*>;

        if (rsrcList == NULL)
            beagleGetResourceList();

        if (implFactory == NULL)
            beagleGetFactoryList();

        loaded = 1;

        // First determine a list of possible resources
        PairedList* possibleResources = new PairedList;
        if (resourceList == NULL || resourceCount == 0) { // No list given
            for(int i=0; i<rsrcList->length; i++)
                possibleResources->push_back(std::make_pair(
                    scoreFlags(preferenceFlags,rsrcList->list[i].supportFlags), // Score
                    i)); // ID
        } else {
            for(int i=0; i<resourceCount; i++)
                possibleResources->push_back(std::make_pair(
                    scoreFlags(preferenceFlags,rsrcList->list[resourceList[i]].supportFlags), // Score
                    resourceList[i])); // ID
        }
        if (requirementFlags != 0) { // If requirements given do restriction
            for(PairedList::iterator it = possibleResources->begin();
                it != possibleResources->end(); ++it) {
                int resource = (*it).second;
                long resourceFlag = rsrcList->list[resource].supportFlags;
                if ( (resourceFlag & requirementFlags) < requirementFlags) {
					if(it==possibleResources->begin()){
	                    possibleResources->remove(*(it));
						it=possibleResources->begin();
					}else
	                    possibleResources->remove(*(it--));
                }
				if(it==possibleResources->end())
					break;
            }
        }

        if (possibleResources->size() == 0) {
            delete possibleResources;
            return BEAGLE_ERROR_NO_RESOURCE;
        }

        beagle::BeagleImpl* bestBeagle = NULL;

        possibleResources->sort(compareOnFirst); // Attempt in rank order, lowest score wins

        int errorCode = BEAGLE_ERROR_NO_RESOURCE;

        // Score each resource-implementation pair given preferences
        RsrcImplList* possibleResourceImplementations = new RsrcImplList;

        for(PairedList::iterator it = possibleResources->begin();
            it != possibleResources->end(); ++it) {
            int resource = (*it).second;
            long resourceRequiredFlags = rsrcList->list[resource].requiredFlags;
            long resourceSupportedFlags = rsrcList->list[resource].supportFlags;
            int resourceScore = (*it).first;
#ifdef BEAGLE_DEBUG_FLOW
            fprintf(stderr,"Possible resource: %s (%d)\n",rsrcList->list[resource].name,resourceScore);
#endif

            for (std::list<beagle::BeagleImplFactory*>::iterator factory =
                 implFactory->begin(); factory != implFactory->end(); factory++) {
                long factoryFlags = (*factory)->getFlags();
#ifdef BEAGLE_DEBUG_FLOW
                fprintf(stderr,"\tExamining implementation: %s\n",(*factory)->getName());
#endif
                if ( ((requirementFlags & factoryFlags) >= requirementFlags) // Factory meets requirementFlags
                    && ((resourceRequiredFlags & factoryFlags) >= resourceRequiredFlags) // Factory meets resourceFlags
                    && ((requirementFlags & resourceSupportedFlags) >= requirementFlags) // Resource meets requirementFlags
                    ) {
                    int implementationScore = scoreFlags(preferenceFlags,factoryFlags);
                    int totalScore = resourceScore + implementationScore;
#ifdef BEAGLE_DEBUG_FLOW
                    fprintf(stderr,"\tPossible implementation: %s (%d)\n",
                            (*factory)->getName(),totalScore);
#endif

                    possibleResourceImplementations->push_back(std::make_pair(totalScore, std::make_pair(resource, (*factory))));

                }
            }
        }

        delete possibleResources;

#ifdef BEAGLE_DEBUG_FLOW
        fprintf(stderr,"\nOriginal list of possible implementations:\n");
        for (RsrcImplList::iterator it = possibleResourceImplementations->begin();
				it != possibleResourceImplementations->end(); ++it) {
        	beagle::BeagleImplFactory* factory = (*it).second.second;
        	fprintf(stderr,"\t %s (%d)\n", factory->getName(), (*it).first);
        }
#endif

        possibleResourceImplementations->sort(compareRsrcImpl);

#ifdef BEAGLE_DEBUG_FLOW
        fprintf(stderr,"\nSorted list of possible implementations:\n");
        for (RsrcImplList::iterator it = possibleResourceImplementations->begin();
				it != possibleResourceImplementations->end(); ++it) {
        	beagle::BeagleImplFactory* factory = (*it).second.second;
        	fprintf(stderr,"\t %s (%d)  (%d)\n", factory->getName(), (*it).first, (*it).second.first);
        }
#endif

        for(RsrcImplList::iterator it = possibleResourceImplementations->begin(); it != possibleResourceImplementations->end(); ++it) {
            int resource = (*it).second.first;
            beagle::BeagleImplFactory* factory = (*it).second.second;

            bestBeagle = factory->createImpl(tipCount, partialsBufferCount,
                                                                compactBufferCount, stateCount,
                                                                patternCount, eigenBufferCount,
                                                                matrixBufferCount, categoryCount,
                                                                scaleBufferCount,
                                                                resource,
                                                                ResourceMap[resource],
                                                                preferenceFlags,
                                                                requirementFlags,
                                                                &errorCode);

            if (bestBeagle != NULL)
                break;
        }

        delete possibleResourceImplementations;

        if (bestBeagle != NULL) {
            int instance = instances->size();
            instances->push_back(bestBeagle);

            int returnValue = bestBeagle->getInstanceDetails(returnInfo);
            if (returnValue == BEAGLE_SUCCESS) {
                returnInfo->resourceName = rsrcList->list[returnInfo->resourceNumber].name;
                // TODO: move implDescription to inside the implementation
                returnInfo->implDescription = (char*) "none";

                returnValue = instance;
            }
            return returnValue;
        }

        // No implementations found or appropriate, return last error code
        return errorCode;
    }
    catch (std::bad_alloc &) {
        return BEAGLE_ERROR_OUT_OF_MEMORY;
    }
    catch (std::out_of_range &) {
        return BEAGLE_ERROR_OUT_OF_RANGE;
    }
    catch (...) {
        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
    }
    loaded = 1;

}

int beagleFinalizeInstance(int instance) {
    DEBUG_FINALIZE_TIME();
    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        delete beagleInstance;
        (*instances)[instance] = NULL;
        return BEAGLE_SUCCESS;
    }
    catch (std::bad_alloc &) {
        return BEAGLE_ERROR_OUT_OF_MEMORY;
    }
    catch (std::out_of_range &) {
        return BEAGLE_ERROR_OUT_OF_RANGE;
    }
    catch (...) {
        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
    }
}

int beagleSetTipStates(int instance,
                 int tipIndex,
                 const int* inStates) {
    DEBUG_START_TIME();
    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->setTipStates(tipIndex, inStates);
        DEBUG_END_TIME();
        return returnValue;
    }
    catch (std::bad_alloc &) {
        return BEAGLE_ERROR_OUT_OF_MEMORY;
    }
    catch (std::out_of_range &) {
        return BEAGLE_ERROR_OUT_OF_RANGE;
    }
    catch (...) {
        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
    }
}

int beagleSetTipPartials(int instance,
                   int tipIndex,
                   const double* inPartials) {
    DEBUG_START_TIME();
    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->setTipPartials(tipIndex, inPartials);
        DEBUG_END_TIME();
        return returnValue;
    }
    catch (std::bad_alloc &) {
        return BEAGLE_ERROR_OUT_OF_MEMORY;
    }
    catch (std::out_of_range &) {
        return BEAGLE_ERROR_OUT_OF_RANGE;
    }
    catch (...) {
        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
    }
}

int beagleSetPartials(int instance,
                int bufferIndex,
                const double* inPartials) {
    DEBUG_START_TIME();
    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->setPartials(bufferIndex, inPartials);
        DEBUG_END_TIME();
        return returnValue;
    }
    catch (std::bad_alloc &) {
        return BEAGLE_ERROR_OUT_OF_MEMORY;
    }
    catch (std::out_of_range &) {
        return BEAGLE_ERROR_OUT_OF_RANGE;
    }
    catch (...) {
        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
    }
}

int beagleGetPartials(int instance, int bufferIndex, int scaleIndex, double* outPartials) {
    DEBUG_START_TIME();
    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->getPartials(bufferIndex, scaleIndex, outPartials);
        DEBUG_END_TIME();
        return returnValue;
    }
    catch (std::bad_alloc &) {
        return BEAGLE_ERROR_OUT_OF_MEMORY;
    }
    catch (std::out_of_range &) {
        return BEAGLE_ERROR_OUT_OF_RANGE;
    }
    catch (...) {
        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
    }
}

int beagleSetEigenDecomposition(int instance,
                          int eigenIndex,
                          const double* inEigenVectors,
                          const double* inInverseEigenVectors,
                          const double* inEigenValues) {
    DEBUG_START_TIME();
    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->setEigenDecomposition(eigenIndex, inEigenVectors,
                                                     inInverseEigenVectors, inEigenValues);
        DEBUG_END_TIME();
        return returnValue;
    }
    catch (std::bad_alloc &) {
        return BEAGLE_ERROR_OUT_OF_MEMORY;
    }
    catch (std::out_of_range &) {
        return BEAGLE_ERROR_OUT_OF_RANGE;
    }
    catch (...) {
        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
    }
}

int beagleSetStateFrequencies(int instance,
                              int stateFrequenciesIndex,
                              const double* inStateFrequencies) {
    DEBUG_START_TIME();
    beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
    if (beagleInstance == NULL)
        return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
    int returnValue = beagleInstance->setStateFrequencies(stateFrequenciesIndex, inStateFrequencies);
    DEBUG_END_TIME();
    return returnValue;
}

int beagleSetCategoryWeights(int instance,
                             int categoryWeightsIndex,
                             const double* inCategoryWeights) {
    DEBUG_START_TIME();
    beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
    if (beagleInstance == NULL)
        return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
    int returnValue = beagleInstance->setCategoryWeights(categoryWeightsIndex, inCategoryWeights);
    DEBUG_END_TIME();
    return returnValue;
}

int beagleSetPatternWeights(int instance,
                            const double* inPatternWeights) {
    DEBUG_START_TIME();
    beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
    if (beagleInstance == NULL)
        return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
    int returnValue = beagleInstance->setPatternWeights(inPatternWeights);
    DEBUG_END_TIME();
    return returnValue;
}

int beagleSetCategoryRates(int instance,
                     const double* inCategoryRates) {
    DEBUG_START_TIME();
//    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->setCategoryRates(inCategoryRates);
        DEBUG_END_TIME();
        return returnValue;
//    }
//    catch (std::bad_alloc &) {
//        return BEAGLE_ERROR_OUT_OF_MEMORY;
//    }
//    catch (std::out_of_range &) {
//        return BEAGLE_ERROR_OUT_OF_RANGE;
//    }
//    catch (...) {
//        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
//    }
}

int beagleSetTransitionMatrix(int instance,
                        int matrixIndex,
                        const double* inMatrix,
                        double paddedValue) {
    DEBUG_START_TIME();
//    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->setTransitionMatrix(matrixIndex, inMatrix, paddedValue);
        DEBUG_END_TIME();
        return returnValue;
//    }
//    catch (std::bad_alloc &) {
//        return BEAGLE_ERROR_OUT_OF_MEMORY;
//    }
//    catch (std::out_of_range &) {
//        return BEAGLE_ERROR_OUT_OF_RANGE;
//    }
//    catch (...) {
//        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
//    }
}

int beagleSetTransitionMatrices(int instance,
                              const int* matrixIndices,
                              const double* inMatrices,
                              const double* paddedValues,
                              int count) {
    DEBUG_START_TIME();
    //    try {
    beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
    if (beagleInstance == NULL)
        return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
    int returnValue = beagleInstance->setTransitionMatrices(matrixIndices, inMatrices, paddedValues, count);
    DEBUG_END_TIME();
    return returnValue;
    //    }
    //    catch (std::bad_alloc &) {
    //        return BEAGLE_ERROR_OUT_OF_MEMORY;
    //    }
    //    catch (std::out_of_range &) {
    //        return BEAGLE_ERROR_OUT_OF_RANGE;
    //    }
    //    catch (...) {
    //        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
    //    }
}

int beagleGetTransitionMatrix(int instance,
							  int matrixIndex,
							  double* outMatrix) {
    DEBUG_START_TIME();
	beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
	if (beagleInstance == NULL)
		return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
    int returnValue = beagleInstance->getTransitionMatrix(matrixIndex,outMatrix);
    DEBUG_END_TIME();
    return returnValue;
}

///////////////////////////
//---TODO: Epoch model---//
///////////////////////////

int beagleConvolveTransitionMatrices(int instance,
		                             const int* firstIndices,
		                             const int* secondIndices,
		                             const int* resultIndices,
		                             const int matrixCount) {
    DEBUG_START_TIME();
	beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);

	if (beagleInstance == NULL) {
		return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
	} else {
        int returnValue = beagleInstance->convolveTransitionMatrices(firstIndices,
                                           secondIndices, resultIndices, matrixCount);
        DEBUG_END_TIME();
        return returnValue;
	}

}//END: beagleConvolveTransitionMatrices

int beagleUpdateTransitionMatrices(int instance,
                             int eigenIndex,
                             const int* probabilityIndices,
                             const int* firstDerivativeIndices,
                             const int* secondDerivativeIndices,
                             const double* edgeLengths,
                             int count) {
    DEBUG_START_TIME();
//    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->updateTransitionMatrices(eigenIndex, probabilityIndices,
                                                        firstDerivativeIndices,
                                                        secondDerivativeIndices, edgeLengths, count);
        DEBUG_END_TIME();
        return returnValue;
//    }
//    catch (std::bad_alloc &) {
//        return BEAGLE_ERROR_OUT_OF_MEMORY;
//    }
//    catch (std::out_of_range &) {
//        return BEAGLE_ERROR_OUT_OF_RANGE;
//    }
//    catch (...) {
//        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
//    }
}

int beagleUpdatePartials(const int instance,
                   const BeagleOperation* operations,
                   int operationCount,
                   int cumulativeScalingIndex) {
    DEBUG_START_TIME();
//    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->updatePartials((const int*)operations, operationCount, cumulativeScalingIndex);
        DEBUG_END_TIME();
        return returnValue;
//    }
//    catch (std::bad_alloc &) {
//        return BEAGLE_ERROR_OUT_OF_MEMORY;
//    }
//    catch (std::out_of_range &) {
//        return BEAGLE_ERROR_OUT_OF_RANGE;
//    }
//    catch (...) {
//        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
//    }
}

int beagleWaitForPartials(const int instance,
                    const int* destinationPartials,
                    int destinationPartialsCount) {
    DEBUG_START_TIME();
//    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->waitForPartials(destinationPartials,
                                                  destinationPartialsCount);
        DEBUG_END_TIME();
        return returnValue;
//    }
//    catch (std::bad_alloc &) {
//        return BEAGLE_ERROR_OUT_OF_MEMORY;
//    }
//    catch (std::out_of_range &) {
//        return BEAGLE_ERROR_OUT_OF_RANGE;
//    }
//    catch (...) {
//        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
//    }
}

int beagleAccumulateScaleFactors(int instance,
						   const int* scalingIndices,
						   int count,
						   int cumulativeScalingIndex) {
    DEBUG_START_TIME();
//    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
         return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->accumulateScaleFactors(scalingIndices, count, cumulativeScalingIndex);
        DEBUG_END_TIME();
        return returnValue;
//    }
//    catch (std::bad_alloc &) {
//        return BEAGLE_ERROR_OUT_OF_MEMORY;
//    }
//    catch (std::out_of_range &) {
//        return BEAGLE_ERROR_OUT_OF_RANGE;
//    }
//    catch (...) {
//        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
//    }
}

int beagleRemoveScaleFactors(int instance,
						   const int* scalingIndices,
						   int count,
						   int cumulativeScalingIndex) {
    DEBUG_START_TIME();
//    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->removeScaleFactors(scalingIndices, count, cumulativeScalingIndex);
        DEBUG_END_TIME();
        return returnValue;
//    }
//    catch (std::bad_alloc &) {
//        return BEAGLE_ERROR_OUT_OF_MEMORY;
//    }
//    catch (std::out_of_range &) {
//        return BEAGLE_ERROR_OUT_OF_RANGE;
//    }
//    catch (...) {
//        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
//    }
}

int beagleResetScaleFactors(int instance,
                      int cumulativeScalingIndex) {
    DEBUG_START_TIME();
//    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->resetScaleFactors(cumulativeScalingIndex);
        DEBUG_END_TIME();
        return returnValue;
//    }
//    catch (std::bad_alloc &) {
//        return BEAGLE_ERROR_OUT_OF_MEMORY;
//    }
//    catch (std::out_of_range &) {
//        return BEAGLE_ERROR_OUT_OF_RANGE;
//    }
//    catch (...) {
//        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
//    }
}

int beagleCopyScaleFactors(int instance,
                           int destScalingIndex,
                           int srcScalingIndex) {
    DEBUG_START_TIME();
    //    try {
    beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
    if (beagleInstance == NULL)
        return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
    int returnValue = beagleInstance->copyScaleFactors(destScalingIndex, srcScalingIndex);
    DEBUG_END_TIME();
    return returnValue;
    //    }
    //    catch (std::bad_alloc &) {
    //        return BEAGLE_ERROR_OUT_OF_MEMORY;
    //    }
    //    catch (std::out_of_range &) {
    //        return BEAGLE_ERROR_OUT_OF_RANGE;
    //    }
    //    catch (...) {
    //        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
    //    }
}

int beagleGetScaleFactors(int instance,
                           int srcScalingIndex,
                           double* scaleFactors) {
    DEBUG_START_TIME();
    //    try {
    beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
    if (beagleInstance == NULL)
        return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
    int returnValue = beagleInstance->getScaleFactors(srcScalingIndex, scaleFactors);
    DEBUG_END_TIME();
    return returnValue;
    //    }
    //    catch (std::bad_alloc &) {
    //        return BEAGLE_ERROR_OUT_OF_MEMORY;
    //    }
    //    catch (std::out_of_range &) {
    //        return BEAGLE_ERROR_OUT_OF_RANGE;
    //    }
    //    catch (...) {
    //        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
    //    }
}

int beagleCalculateRootLogLikelihoods(int instance,
                                      const int* bufferIndices,
                                      const int* categoryWeightsIndices,
                                      const int* stateFrequenciesIndices,
                                      const int* cumulativeScaleIndices,
                                      int count,
                                      double* outSumLogLikelihood) {
    DEBUG_START_TIME();
//    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->calculateRootLogLikelihoods(bufferIndices, categoryWeightsIndices,
                                                           stateFrequenciesIndices,
                                                           cumulativeScaleIndices,
                                                           count,
                                                           outSumLogLikelihood);
        DEBUG_END_TIME();
        return returnValue;
//    }
//    catch (std::bad_alloc &) {
//        return BEAGLE_ERROR_OUT_OF_MEMORY;
//    }
//    catch (std::out_of_range &) {
//        return BEAGLE_ERROR_OUT_OF_RANGE;
//    }
//    catch (...) {
//        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
//    }


}

int beagleCalculateEdgeLogLikelihoods(int instance,
                                      const int* parentBufferIndices,
                                      const int* childBufferIndices,
                                      const int* probabilityIndices,
                                      const int* firstDerivativeIndices,
                                      const int* secondDerivativeIndices,
                                      const int* categoryWeightsIndices,
                                      const int* stateFrequenciesIndices,
                                      const int* cumulativeScaleIndices,
                                      int count,
                                      double* outSumLogLikelihood,
                                      double* outSumFirstDerivative,
                                      double* outSumSecondDerivative) {
    DEBUG_START_TIME();
//    try {
        beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
        if (beagleInstance == NULL)
            return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
        int returnValue = beagleInstance->calculateEdgeLogLikelihoods(parentBufferIndices, childBufferIndices,
                                                           probabilityIndices,
                                                           firstDerivativeIndices,
                                                           secondDerivativeIndices, categoryWeightsIndices,
                                                           stateFrequenciesIndices, cumulativeScaleIndices,
                                                           count,
                                                           outSumLogLikelihood, outSumFirstDerivative,
                                                           outSumSecondDerivative);
        DEBUG_END_TIME();
        return returnValue;
//    }
//    catch (std::bad_alloc &) {
//        return BEAGLE_ERROR_OUT_OF_MEMORY;
//    }
//    catch (std::out_of_range &) {
//        return BEAGLE_ERROR_OUT_OF_RANGE;
//    }
//    catch (...) {
//        return BEAGLE_ERROR_UNIDENTIFIED_EXCEPTION;
//    }
}

int beagleGetSiteLogLikelihoods(int instance,
                                double* outLogLikelihoods) {
    DEBUG_START_TIME();
    beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
    if (beagleInstance == NULL)
        return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
    int returnValue = beagleInstance->getSiteLogLikelihoods(outLogLikelihoods);
    DEBUG_END_TIME();
    return returnValue;
}

int beagleGetSiteDerivatives(int instance,
                             double* outFirstDerivatives,
                             double* outSecondDerivatives) {
    DEBUG_START_TIME();
    beagle::BeagleImpl* beagleInstance = beagle::getBeagleInstance(instance);
    if (beagleInstance == NULL)
        return BEAGLE_ERROR_UNINITIALIZED_INSTANCE;
    int returnValue = beagleInstance->getSiteDerivatives(outFirstDerivatives, outSecondDerivatives);
    DEBUG_END_TIME();
    return returnValue;
}

