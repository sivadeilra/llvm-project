template<lifetime @a, lifetime @b>
requires @a : @b
int^@b shorten(int^@a x) {
  return x;  // OK: @a longer than @b, can return @b
}

template<lifetime @a, lifetime @b>
requires @a : @b
int^@a widen(int^@b x) {
  return x;  // ERROR: @b shorter than @a, cannot return @a
}
