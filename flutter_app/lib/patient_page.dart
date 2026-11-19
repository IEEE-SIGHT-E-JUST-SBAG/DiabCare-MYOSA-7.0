import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';
import 'models.dart';

/// Patient view: activity, steps, simple status. Neutral wording, no diagnosis.
class PatientPage extends StatelessWidget {
  final String pid;
  const PatientPage({super.key, required this.pid});

  Future<void> _cmd(Map<String, Object?> data) => FirebaseDatabase.instance.ref('patients/$pid/cmd').update({
        ...data, 'seq': DateTime.now().millisecondsSinceEpoch ~/ 1000,
      });

  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(title: const Text('My walking'), actions: [
          IconButton(icon: const Icon(Icons.logout), onPressed: () => FirebaseAuth.instance.signOut()),
        ]),
        body: StreamBuilder<DatabaseEvent>(
          stream: FirebaseDatabase.instance.ref('patients/$pid/live').onValue,
          builder: (_, snap) {
            final raw = asMap(snap.data?.snapshot.value);
            if (raw.isEmpty) return const Center(child: Text('Waiting for your device…'));
            final l = Live(raw);
            final msg = l.err ? 'Sensor problem - the device is retrying.'
                : !l.baseDone ? 'Learning your usual walking pattern (${l.base}%). Just walk as usual.'
                : l.risk == 2 ? 'Please consider contacting your clinic for a follow-up.'
                : l.risk == 1 ? 'Some changes in your walking were noted. Your clinician can see them.'
                : 'Your walking looks consistent with your usual pattern.';
            return ListView(padding: const EdgeInsets.all(16), children: [
              Container(
                padding: const EdgeInsets.all(20),
                decoration: BoxDecoration(color: levelColor(l.baseDone ? l.risk : null, err: l.err), borderRadius: BorderRadius.circular(16)),
                child: Column(children: [
                  Text(l.err ? 'SENSOR ERROR' : !l.baseDone ? 'LEARNING' : levelNames[(l.risk ?? 0).clamp(0, 2)].toUpperCase(),
                      style: const TextStyle(color: Colors.white, fontSize: 30, fontWeight: FontWeight.bold)),
                  const SizedBox(height: 6),
                  Text(msg, textAlign: TextAlign.center, style: const TextStyle(color: Colors.white)),
                  if (l.demo) const Text('DEMO - synthetic data', style: TextStyle(color: Colors.white70)),
                ]),
              ),
              const SizedBox(height: 12),
              Wrap(spacing: 10, runSpacing: 10, children: [
                _tile('Activity', actNames[(l.act ?? 0).clamp(0, 2)]),
                _tile('Steps today', '${l.steps ?? "--"}'),
                _tile('Cadence', '${l.cad ?? "--"} /min'),
                _tile('Sensor on', l.leg == 'L' ? 'Left ankle' : l.leg == 'R' ? 'Right ankle' : '?'),
              ]),
              const SizedBox(height: 16),
              const Text('Which ankle is the device on? (one leg is recorded at a time)'),
              const SizedBox(height: 6),
              SegmentedButton<String>(
                segments: const [ButtonSegment(value: 'L', label: Text('Left')), ButtonSegment(value: 'R', label: Text('Right'))],
                selected: {l.leg == 'R' ? 'R' : 'L'},
                onSelectionChanged: (s) => _cmd({'leg': s.first}),
              ),
              const SizedBox(height: 8),
              SwitchListTile(
                title: const Text('Demo mode (synthetic data)'),
                value: l.demo, onChanged: (v) => _cmd({'demo': v}),
              ),
              if ((l.queued ?? 0) > 0) Text('${l.queued} records waiting to upload (offline buffer).', style: const TextStyle(color: Colors.grey)),
              const SizedBox(height: 12),
              const Text('This is an awareness tool, not a medical device. It does not diagnose any condition.',
                  style: TextStyle(fontSize: 11, color: Colors.grey)),
            ]);
          },
        ),
      );

  Widget _tile(String k, String v) => SizedBox(
        width: 160,
        child: Card(child: Padding(padding: const EdgeInsets.all(12), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(k, style: const TextStyle(fontSize: 11, color: Colors.grey)), Text(v, style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
        ]))),
      );
}
