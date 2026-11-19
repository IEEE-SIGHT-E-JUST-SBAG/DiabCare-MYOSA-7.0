import 'package:fl_chart/fl_chart.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';
import 'models.dart';

/// Clinician view: risk-sorted patient list -> trend graphs, baseline comparison, follow-up indicator.
class ClinicianPage extends StatelessWidget {
  const ClinicianPage({super.key});
  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(title: const Text('Patients'), actions: [
          IconButton(icon: const Icon(Icons.logout), onPressed: () => FirebaseAuth.instance.signOut()),
        ]),
        body: StreamBuilder<DatabaseEvent>(
          stream: FirebaseDatabase.instance.ref('index').onValue,
          builder: (_, snap) {
            final m = asMap(snap.data?.snapshot.value);
            if (m.isEmpty) return const Center(child: Text('No patients yet.'));
            final ids = m.keys.toList()
              ..sort((a, b) => ((asMap(m[b])['risk'] as num?) ?? -1).compareTo((asMap(m[a])['risk'] as num?) ?? -1));
            return ListView(children: [
              for (final id in ids) _row(context, id, asMap(m[id])),
            ]);
          },
        ),
      );

  Widget _row(BuildContext c, String id, Map<String, dynamic> j) {
    final risk = (j['risk'] as num?)?.toInt();
    final base = (j['base'] as num?)?.toInt() ?? 0;
    final err = j['err'] == true;
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 5),
      child: ListTile(
        leading: CircleAvatar(backgroundColor: levelColor(base >= 100 ? risk : null, err: err), child: const Icon(Icons.person, color: Colors.white)),
        title: Text('$id  ·  ${err ? "Sensor error" : base < 100 ? "Learning baseline $base%" : levelNames[(risk ?? 0).clamp(0, 2)]}${j['demo'] == true ? " (demo)" : ""}'),
        trailing: const Icon(Icons.chevron_right),
        onTap: () => Navigator.push(c, MaterialPageRoute(builder: (_) => PatientDetail(pid: id, demo: j['demo'] == true))),
      ),
    );
  }
}

class PatientDetail extends StatelessWidget {
  final String pid; final bool demo;
  const PatientDetail({super.key, required this.pid, required this.demo});

  @override
  Widget build(BuildContext context) {
    final base = demo ? 'patients/$pid/demo' : 'patients/$pid';
    return Scaffold(
      appBar: AppBar(title: Text(pid)),
      body: StreamBuilder<DatabaseEvent>(
        stream: FirebaseDatabase.instance.ref('$base/daily').orderByKey().limitToLast(60).onValue,
        builder: (_, snap) {
          final raw = asMap(snap.data?.snapshot.value);
          final days = raw.entries.map((e) => Daily(int.tryParse(e.key) ?? 0, asMap(e.value))).toList()
            ..sort((a, b) => a.day.compareTo(b.day));
          if (days.isEmpty) return const Center(child: Text('No daily summaries yet.'));
          final last = days.last;
          return ListView(padding: const EdgeInsets.all(12), children: [
            Container(
              padding: const EdgeInsets.all(14),
              decoration: BoxDecoration(color: levelColor(last.risk), borderRadius: BorderRadius.circular(12)),
              child: Text('Follow-up indicator: ${levelNames[last.risk.clamp(0, 2)]}${demo ? "  (DEMO - synthetic)" : ""}',
                  style: const TextStyle(color: Colors.white, fontSize: 20, fontWeight: FontWeight.bold)),
            ),
            const SizedBox(height: 8),
            const Text('Active indicators on the latest day', style: TextStyle(fontWeight: FontWeight.bold)),
            Wrap(spacing: 6, children: [
              for (var b = 0; b < flagNames.length; b++)
                if ((last.mask >> b) & 1 == 1) Chip(label: Text(flagNames[b], style: const TextStyle(fontSize: 11)), backgroundColor: Colors.orange.shade100),
              if (last.mask == 0) const Chip(label: Text('none')),
            ]),
            const SizedBox(height: 8),
            for (final m in const [0, 1, 4, 5]) _trend(days, m),
            const SizedBox(height: 6),
            _riskStrip(days),
            const SizedBox(height: 10),
            const Text('Follow-up prioritization only. Not a diagnosis; loading values are an inertial proxy, not plantar pressure.',
                style: TextStyle(fontSize: 11, color: Colors.grey)),
          ]);
        },
      ),
    );
  }

  Widget _trend(List<Daily> days, int m) {
    final spots = <FlSpot>[for (var i = 0; i < days.length; i++) if (days[i].met.length > m && !days[i].met[m].isNaN) FlSpot(i.toDouble(), days[i].met[m])];
    final bm = days.last.bm.length > m ? days.last.bm[m] : double.nan;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text('${metricNames[m]}   (dashed = personal baseline)', style: const TextStyle(fontSize: 12)),
        SizedBox(
          height: 130,
          child: LineChart(LineChartData(
            lineBarsData: [
              LineChartBarData(spots: spots, color: Colors.teal, barWidth: 2, dotData: const FlDotData(show: false)),
              if (!bm.isNaN && spots.length > 1)
                LineChartBarData(spots: [FlSpot(0, bm), FlSpot((days.length - 1).toDouble(), bm)], color: Colors.grey, barWidth: 1,
                    dashArray: [5, 4], dotData: const FlDotData(show: false)),
            ],
            titlesData: const FlTitlesData(
              topTitles: AxisTitles(sideTitles: SideTitles(showTitles: false)),
              rightTitles: AxisTitles(sideTitles: SideTitles(showTitles: false)),
              bottomTitles: AxisTitles(sideTitles: SideTitles(showTitles: false)),
            ),
          )),
        ),
      ]),
    );
  }

  Widget _riskStrip(List<Daily> days) => Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        const Text('Follow-up level per day', style: TextStyle(fontSize: 12)),
        const SizedBox(height: 4),
        Row(children: [for (final d in days) Expanded(child: Container(height: 14, color: levelColor(d.risk)))]),
      ]);
}
