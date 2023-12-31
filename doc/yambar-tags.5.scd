yambar-tags(5)

# NAME
yambar-tags - configuration file

# DESCRIPTION

Tags are the data carriers; it is through tags that modules expose
their information. Each module defines its own set of tags.

The available tag *types* are:

[[ *Type*
:< *Description*
|  string
:  Value is a string. Rendered as-is by the _string_ particle.
|  int
:  Value is an integer. Rendered in base 10 by the _string_ particle.
|  bool
:  Value is true or false. Rendered as "true" or "false" by the _string_
   particle
|  float
:  Value is a float. Rendered in base 10, with two decimal digits by the
   _string_ particle
|  range
:  Value is an integer, with a minimum and maximum value associated
   with it. By default, the _string_ particle renders the value. The
   *:min* or *:max* suffixes may be added to instead render the
   minimum or maximum value (_\"{tag_name:min}\"_).
|  realtime
:  Value is an integer that changes in a predictable manner (in
   "realtime"). This allows the particle to update itself
   periodically. Only supported by the
   *yambar-particle-progress-bar*(5). Other particles can still render
   the tag's value. And, the _string_ particle recognizes the *:unit*
   suffix, which will be translated to a "s" for a tag with "seconds"
   resolution, or "ms" for one with "milliseconds" resolution.

# FORMATTING

A tag may be followed by one or more formatters that alter the tags
rendition.

Formatters are added by appending a ':' separated list of formatter
names:

	"{tag_name:max:hex}"

In the table below, "kind" describes the type of action performed by
the formatter:

- *format*: changes the representation of the tag's value
- *selector*: changes what to render

In general, formatters of the same kind cannot be combined; if
multiple formatters of the same kind are specified, the last one will
be used.

[[ *Formatter*
:[ *Kind*
:[ *Applies to*
:< *Description*
|  [0]<number>[.]
:  format
:  Numeric tags (integer and floats)
:  The width reserved to the field. The leading '0' is optional and
   indicates zero padding, as opposed to space padding. The trailing
   '.' is also optional
|  .<number>
:  format
:  Float tags
:  How many decimals to print
|  [0]<N>[.]<M>
:  format
:  N: numeric tags, M: float tags
:  Combined version of the two previous formatters
|  hex
:  format
:  All tag types
:  Renders a tag's value in hex
|  oct
:  format
:  All tag types
:  Renders a tag's value in octal
|  %
:  format
:  Range tags
:  Renders a range tag's value as a percentage value
|  kb, mb, gb
:  format
:  All tag types
:  Renders a tag's value (in decimal) divided by 1000, 1000^2 or
   1000^3. Note: no unit suffix is appended)
|  kib, mib, gib
:  format
:  All tag types
:  Same as *kb*, *mb* and *gb*, but divide by 1024^n instead of 1000^n.
|  min
:  selector
:  Range tags
:  Renders a range tag's minimum value
|  max
:  selector
:  Range tags
:  Renders a range tag's maximum value
|  unit
:  selector
:  Realtime tags
:  Renders a realtime tag's unit (e.g. "s", or "ms")

# EXAMPLES

- A numeric (float or int) tag with at least 3 digits, zero-padded if
  necessary:

```
{tag:03}
```

- A float tag with 2 decimals:

```
{tag:.2}
```

- A "byte count" tag in gigabytes:

```
{tag:gib}GB
```
