-- everything lives in the namespace
open Geo.Util Geo.Util.Shape in
example : measure SQUARE 3 = 21 := by decide
example : Geo.Util.measure Geo.Util.Shape.LINE 5 = 5 := by decide
example : Geo.Util.perimeter 2 = 8 := by decide
