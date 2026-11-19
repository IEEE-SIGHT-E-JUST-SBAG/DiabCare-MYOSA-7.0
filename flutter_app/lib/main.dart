import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';
import 'clinician_page.dart';
import 'firebase_options.dart';
import 'patient_page.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Firebase.initializeApp(options: DefaultFirebaseOptions.currentPlatform);
  runApp(const DiabCareApp());
}

class DiabCareApp extends StatelessWidget {
  const DiabCareApp({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'DiabCare',
        theme: ThemeData(colorSchemeSeed: Colors.teal, useMaterial3: true),
        home: StreamBuilder<User?>(
          stream: FirebaseAuth.instance.authStateChanges(),
          builder: (_, s) => s.data == null ? const LoginPage() : RoleGate(user: s.data!),
        ),
      );
}

class LoginPage extends StatefulWidget {
  const LoginPage({super.key});
  @override
  State<LoginPage> createState() => _LoginPageState();
}

class _LoginPageState extends State<LoginPage> {
  final email = TextEditingController(), pass = TextEditingController();
  String? error; bool busy = false;

  Future<void> _login() async {
    setState(() { busy = true; error = null; });
    try {
      await FirebaseAuth.instance.signInWithEmailAndPassword(email: email.text.trim(), password: pass.text);
    } on FirebaseAuthException catch (e) {
      setState(() => error = e.message);
    }
    if (mounted) setState(() => busy = false);
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        body: Center(
          child: SizedBox(
            width: 340,
            child: Column(mainAxisSize: MainAxisSize.min, children: [
              const Icon(Icons.directions_walk, size: 56),
              const Text('DiabCare', style: TextStyle(fontSize: 28, fontWeight: FontWeight.bold)),
              const SizedBox(height: 16),
              TextField(controller: email, decoration: const InputDecoration(labelText: 'E-mail')),
              TextField(controller: pass, obscureText: true, decoration: const InputDecoration(labelText: 'Password')),
              if (error != null) Padding(padding: const EdgeInsets.all(8), child: Text(error!, style: const TextStyle(color: Colors.red))),
              const SizedBox(height: 12),
              FilledButton(onPressed: busy ? null : _login, child: const Text('Sign in')),
              const SizedBox(height: 16),
              const Text('Awareness tool - not a medical device.', style: TextStyle(fontSize: 11, color: Colors.grey)),
            ]),
          ),
        ),
      );
}

/// Decides patient vs clinician view from the access map in the database.
class RoleGate extends StatelessWidget {
  final User user;
  const RoleGate({super.key, required this.user});

  Future<(bool, String?)> _role() async {
    final db = FirebaseDatabase.instance;
    final clin = (await db.ref('clinicians/${user.uid}').get()).value == true;
    final pid = (await db.ref('patientUsers/${user.uid}').get()).value as String?;
    return (clin, pid);
  }

  @override
  Widget build(BuildContext context) => FutureBuilder<(bool, String?)>(
        future: _role(),
        builder: (_, s) {
          if (!s.hasData) return Scaffold(body: Center(child: s.hasError ? Text('${s.error}') : const CircularProgressIndicator()));
          final (clin, pid) = s.data!;
          if (clin) return const ClinicianPage();
          if (pid != null) return PatientPage(pid: pid);
          return Scaffold(
            appBar: AppBar(actions: [IconButton(icon: const Icon(Icons.logout), onPressed: () => FirebaseAuth.instance.signOut())]),
            body: const Center(child: Text('This account has no role yet.\nAsk the administrator to add it to the access map.', textAlign: TextAlign.center)),
          );
        },
      );
}
