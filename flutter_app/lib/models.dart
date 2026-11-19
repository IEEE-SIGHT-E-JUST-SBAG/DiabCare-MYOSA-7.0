import 'package:flutter/material.dart';

const metricNames = ['Steps', 'Walking (min)', 'Bouts', 'Mean bout length (steps)', 'Cadence (steps/min)', 'Longest static period (min)'];
const flagNames = [
  'Asymmetry: stride time', 'Asymmetry: stride variability', 'Asymmetry: stride regularity',
  'Asymmetry: heel-strike peak', 'Asymmetry: loading rate', 'Foot-loading pattern (inertial proxy)', 'Walking-behavior change',
];
const levelNames = ['Stable', 'Watch', 'Review'];
const actNames = ['Static', 'Walking', 'Other movement'];

Color levelColor(int? l, {bool err = false}) {
  if (err || l == null) return Colors.grey;
  return [Colors.green.shade700, Colors.orange.shade700, Colors.red.shade700][l.clamp(0, 2)];
}

Map<String, dynamic> asMap(Object? v) =>
    v is Map ? v.map((k, v) => MapEntry(k.toString(), v)) : <String, dynamic>{};
List<double> asDoubles(Object? v) =>
    v is List ? v.map((e) => (e as num?)?.toDouble() ?? double.nan).toList() : <double>[];

class Live {
  final int? risk, act, steps, cad, base, asym, rssi, queued;
  final String leg;
  final bool demo, err, baseDone, load;
  Live(Map<String, dynamic> j)
      : risk = (j['risk'] as num?)?.toInt(), act = (j['act'] as num?)?.toInt(),
        steps = (j['steps'] as num?)?.toInt(), cad = (j['cad'] as num?)?.toInt(),
        base = (j['base'] as num?)?.toInt(), asym = (j['asym'] as num?)?.toInt(),
        rssi = (j['rssi'] as num?)?.toInt(), queued = (j['queued'] as num?)?.toInt(),
        leg = (j['leg'] ?? '?').toString(), demo = j['demo'] == true, err = j['err'] == true,
        baseDone = j['baseDone'] == true, load = j['load'] == true;
}

class Daily {
  final int day, mask, risk;
  final List<double> met, sym, bm, bs;
  final double abn;
  Daily(this.day, Map<String, dynamic> j)
      : mask = (j['mask'] as num?)?.toInt() ?? 0, risk = (j['risk'] as num?)?.toInt() ?? 0,
        met = asDoubles(j['met']), sym = asDoubles(j['sym']), bm = asDoubles(j['bm']),
        bs = asDoubles(j['bs']), abn = (j['abn'] as num?)?.toDouble() ?? -1;
}
