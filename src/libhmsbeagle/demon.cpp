#include <Rcpp.h>
#include "beagle.h"
using namespace Rcpp;


//' @useDynLib beagleR
//' @importFrom Rcpp sourceCpp
// [[Rcpp::export]]
SEXP BeagleInstanceDetailsR(){
  XPtr<BeagleInstanceDetails> returnInfo(new BeagleInstanceDetails(), true);
  return returnInfo;
}
