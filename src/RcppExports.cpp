// Generated by using Rcpp::compileAttributes() -> do not edit by hand
// Generator token: 10BE3573-1514-4C36-9D1C-5A225CD40393

#include <Rcpp.h>

using namespace Rcpp;

// BeagleInstanceDetailsR
SEXP BeagleInstanceDetailsR();
RcppExport SEXP beagleR_BeagleInstanceDetailsR() {
BEGIN_RCPP
    Rcpp::RObject rcpp_result_gen;
    Rcpp::RNGScope rcpp_rngScope_gen;
    rcpp_result_gen = Rcpp::wrap(BeagleInstanceDetailsR());
    return rcpp_result_gen;
END_RCPP
}

static const R_CallMethodDef CallEntries[] = {
    {"beagleR_BeagleInstanceDetailsR", (DL_FUNC) &beagleR_BeagleInstanceDetailsR, 0},
    {NULL, NULL, 0}
};

RcppExport void R_init_beagleR(DllInfo *dll) {
    R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
    R_useDynamicSymbols(dll, FALSE);
}
