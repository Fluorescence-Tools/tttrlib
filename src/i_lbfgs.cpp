// minimize f(x,p) using BFGS algorithm

#include "i_lbfgs.h"


int fjac1(void (*f)(double *, double *), double *x, int m, int n, double eps, double *fjac) {

    int ij;
    double h, temp;
    double *fvec = new double[m];
    double *wa = new double[m];

    f(x, fvec);

    ij = 0;
    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * abs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, wa);
        x[j] = temp;

        for (int i = 0; i < m; i++) {
            fjac[ij] = (wa[i] - fvec[i]) / h;
            ij += 1;    /* fjac[i+m*j] */
        }
    }

    delete[] fvec;
    delete[] wa;
    return 0;
}

int fgrad1(void (*f)(double *, double &), double *x, int n, double eps, double *fgrad) {

    double h, temp;
    double fval;
    double w;

    f(x, fval);

    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * abs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, w);
        x[j] = temp;

        fgrad[j] = (w - fval) / h;
    }

    return 0;
}

int fjac2(void (*f)(double *, double *), double *x, int m, int n, double eps, double *fjac) {

    int ij;
    double h, temp;
    double *wa1 = new double[m];
    double *wa2 = new double[m];

    ij = 0;
    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * abs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, wa1);
        x[j] = temp - h;
        f(x, wa2);
        x[j] = temp;

        for (int i = 0; i < m; i++) {
            fjac[ij] = 0.5 * (wa1[i] - wa2[i]) / h;
            ij += 1;    /* fjac[i+m*j] */
        }
    }

    delete[] wa1;
    delete[] wa2;
    return 0;
}

int fgrad2(void (*f)(double *, double &), double *x, int n, double eps, double *fgrad) {

    double h, temp;
    double w1, w2;

    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * abs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, w1);
        x[j] = temp - h;
        f(x, w2);
        x[j] = temp;

        fgrad[j] = 0.5 * (w1 - w2) / h;
    }

    return 0;
}

int fjac4(void (*f)(double *, double *), double *x, int m, int n, double eps, double *fjac) {

    const double c1 = 2. / 3.;
    const double c2 = 1. / 12.;

    int ij;
    double h, temp;
    double *wa1 = new double[m];
    double *wa2 = new double[m];

    ij = 0;
    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * abs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, wa1);
        x[j] = temp - h;
        f(x, wa2);

        for (int i = 0; i < m; i++) {
            fjac[ij] = c1 * (wa1[i] - wa2[i]) / h;
            ij += 1;    /* fjac[i+m*j] */
        }

        ij -= m;
        x[j] = temp + 2. * h;
        f(x, wa1);
        x[j] = temp - 2. * h;
        f(x, wa2);
        x[j] = temp;

        for (int i = 0; i < m; i++) {
            fjac[ij] += c2 * (wa2[i] - wa1[i]) / h;
            ij += 1;    /* fjac[i+m*j] */
        }

    }

    delete[] wa1;
    delete[] wa2;
    return 0;
}

int fgrad4(void (*f)(double *, double &), double *x, int n, double eps, double *fgrad) {

    const double c1 = 2. / 3.;
    const double c2 = 1. / 12.;

    double h, temp;
    double w1, w2;

    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * abs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, w1);
        x[j] = temp - h;
        f(x, w2);

        fgrad[j] = c1 * (w1 - w2) / h;

        x[j] = temp + 2. * h;
        f(x, w1);
        x[j] = temp - 2. * h;
        f(x, w2);
        x[j] = temp;

        fgrad[j] += c2 * (w2 - w1) / h;
    }

    return 0;
}


////////////////////////////////////// gradient ////////////////////////////////////

void bfgs::fgrad1(ap::real_1d_array &x, double &fval, ap::real_1d_array &fgrad) {

    double h, temp, w;
    int j = 1;

    for (int i = 0; i < N; i++)
        if (!fixed[i]) xd[i] = x(j++);

    fval = f(xd, pcopy);

    j = 1;
    for (int i = 0; i < N; i++) {
        if (fixed[i]) continue;
        temp = xd[i];
        h = sqrt_eps * std::abs(temp);
        if (h == 0.) h = sqrt_eps;
        xd[i] = temp + h;
        w = f(xd, pcopy);
        xd[i] = temp;

        fgrad(j++) = (w - fval) / h;
    }

}

///////////////////////////////////////// 2 points //////////////////////////////////////

void bfgs::fgrad2(ap::real_1d_array &x, double &fval, ap::real_1d_array &fgrad) {

    double h, temp, w1, w2;
    int j = 1;

    for (int i = 0; i < N; i++)
        if (!fixed[i]) xd[i] = x(j++);

    fval = f(xd, pcopy);

    j = 1;
    for (int i = 0; i < N; i++) {
        if (fixed[i]) continue;
        temp = xd[i];
        h = sqrt_eps * std::abs(temp);
        if (h == 0.) h = sqrt_eps;
        xd[i] = temp + h;
        w1 = f(xd, pcopy);
        xd[i] = temp - h;
        w2 = f(xd, pcopy);
        xd[i] = temp;

        fgrad(j++) = 0.5 * (w1 - w2) / h;
    }

}

///////////////////////////////////////// 4 points //////////////////////////////////////

void bfgs::fgrad4(ap::real_1d_array &x, double &fval, ap::real_1d_array &fgrad) {

    const double c1 = 2. / 3.;
    const double c2 = 1. / 12.;

    double h, temp, w1, w2;
    int j = 1;

    for (int i = 0; i < N; i++)
        if (!fixed[i]) xd[i] = x(j++);

    fval = f(xd, pcopy);

    j = 1;
    for (int i = 0; i < N; i++) {
        if (fixed[i]) continue;
        temp = xd[i];
        h = sqrt_eps * std::abs(temp);
        if (h == 0.) h = sqrt_eps;
        xd[i] = temp + h;
        w1 = f(xd, pcopy);
        xd[i] = temp - h;
        w2 = f(xd, pcopy);

        fgrad(j) = c1 * (w1 - w2) / h;

        xd[i] = temp + 2. * h;
        w1 = f(xd, pcopy);
        xd[i] = temp - 2. * h;
        w2 = f(xd, pcopy);
        xd[i] = temp;

        fgrad(j++) += c2 * (w2 - w1) / h;
    }
}