#pragma once
// First-order IIR section shared by the behavioural models.

namespace aes {

// (n0 + n1 s) / (1 + d1 s) discretised with the bilinear transform.
class FirstOrder {
public:
    FirstOrder() = default;
    FirstOrder(double fs, double n0, double n1, double d1) {
        const double k = 2.0 * fs;
        const double a0 = 1.0 + d1 * k;
        b0_ = (n0 + n1 * k) / a0;
        b1_ = (n0 - n1 * k) / a0;
        a1_ = (1.0 - d1 * k) / a0;
    }

    // Replaces the coefficients but keeps the state, so knob moves do not click.
    void retune(const FirstOrder& other) { b0_ = other.b0_; b1_ = other.b1_; a1_ = other.a1_; }

    double process(double x) {
        const double y = b0_ * x + b1_ * x1_ - a1_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    double b0_ = 0, b1_ = 0, a1_ = 0, x1_ = 0, y1_ = 0;
};

}  // namespace aes
