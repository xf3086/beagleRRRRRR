#include <Rcpp.h>
using namespace Rcpp;
#include "beagle.h"




//' @useDynLib beagleR
//' @importFrom Rcpp sourceCpp


// [[Rcpp::export]]
SEXP BeagleInstanceDetailsR(){
  XPtr<BeagleInstanceDetails> returnInfo(new BeagleInstanceDetails(), true);
  return returnInfo;
}

