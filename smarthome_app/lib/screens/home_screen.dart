import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  Map<String, dynamic> _status = {};
  bool _isLoading = true;
  String? _errorMessage;
  Timer? _timer;

  @override
  void initState() {
    super.initState();
    _fetchStatus();
    // Update every 10 seconds
    _timer = Timer.periodic(const Duration(seconds: 10), (_) => _fetchStatus());
  }

  Future<void> _fetchStatus() async {
    setState(() {
      _isLoading = true;
      _errorMessage = null;
    });

    try {
      final prefs = await SharedPreferences.getInstance();
      final token = prefs.getString('auth_token');

      if (token == null) {
        throw Exception('No token found. Please login again.');
      }

      print('Token being sent: $token');
      print('Token length: ${token.length}');

      final response = await http.get(
        Uri.parse('http://192.168.1.145:5000/status'),
        headers: {
          'Content-Type': 'application/json',
          'Authorization': 'Bearer $token',
        },
      );

      if (response.statusCode == 200) {
        setState(() {
          _status = jsonDecode(response.body);
          _isLoading = false;
        });
      } else if (response.statusCode == 401) {
        // Token expired or invalid → logout
        await prefs.remove('auth_token');
        if (mounted) {
          Navigator.pushReplacementNamed(context, '/');
        }
      } else {
        setState(() {
          _errorMessage = 'Server error: ${response.statusCode}';
          _isLoading = false;
        });
      }
    } catch (e) {
      setState(() {
        _errorMessage = 'Failed to load status: $e';
        _isLoading = false;
      });
    }
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Smart Home'),
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: _fetchStatus,
          ),
        ],
      ),
      body: _isLoading
          ? const Center(child: CircularProgressIndicator())
          : _errorMessage != null
              ? Center(
                  child: Padding(
                    padding: const EdgeInsets.all(24.0),
                    child: Text(
                      _errorMessage!,
                      style: const TextStyle(color: Colors.red, fontSize: 18),
                      textAlign: TextAlign.center,
                    ),
                  ),
                )
              : SingleChildScrollView(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      // Temperature & Humidity Card
                      Card(
                        elevation: 4,
                        child: Padding(
                          padding: const EdgeInsets.all(16.0),
                          child: Column(
                            children: [
                              const Text(
                                'Current Conditions',
                                style: TextStyle(
                                  fontSize: 20,
                                  fontWeight: FontWeight.bold,
                                ),
                              ),
                              const SizedBox(height: 16),
                              Row(
                                mainAxisAlignment:
                                    MainAxisAlignment.spaceEvenly,
                                children: [
                                  _buildInfoItem(
                                    icon: Icons.thermostat,
                                    label: 'Temperature',
                                    value:
                                        '${_status['temperature'] ?? '--'} °C',
                                  ),
                                  _buildInfoItem(
                                    icon: Icons.water_drop,
                                    label: 'Humidity',
                                    value: '${_status['humidity'] ?? '--'} %',
                                  ),
                                ],
                              ),
                            ],
                          ),
                        ),
                      ),

                      const SizedBox(height: 24),

                      // Door & Motion status
                      Card(
                        elevation: 4,
                        child: Padding(
                          padding: const EdgeInsets.all(16.0),
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              const Text(
                                'Security',
                                style: TextStyle(
                                  fontSize: 18,
                                  fontWeight: FontWeight.bold,
                                ),
                              ),
                              const SizedBox(height: 12),
                              ListTile(
                                leading: Icon(
                                  Icons.door_front_door,
                                  color: _status['door'] == 'open'
                                      ? Colors.orange
                                      : Colors.green,
                                  size: 36,
                                ),
                                title: const Text('Door'),
                                subtitle: Text(
                                  _status['door']?.toString().toUpperCase() ??
                                      'Unknown',
                                  style: TextStyle(
                                    color: _status['door'] == 'open'
                                        ? Colors.orange
                                        : Colors.green,
                                    fontWeight: FontWeight.bold,
                                  ),
                                ),
                              ),
                              ListTile(
                                leading: Icon(
                                  Icons.sensors,
                                  color: _status['motion_detected'] == true
                                      ? Colors.red
                                      : Colors.grey,
                                  size: 36,
                                ),
                                title: const Text('Motion'),
                                subtitle: Text(
                                  _status['motion_detected'] == true
                                      ? 'Detected'
                                      : 'No motion',
                                ),
                              ),
                              if (_status['last_access'] != null) ...[
                                ListTile(
                                  leading: const Icon(Icons.login, size: 36),
                                  title: const Text('Last Access'),
                                  subtitle: Text(_status['last_access']),
                                ),
                              ],
                            ],
                          ),
                        ),
                      ),

                      const SizedBox(height: 32),

                      // Navigation buttons
                      ElevatedButton.icon(
                        icon: const Icon(Icons.lock_open),
                        label: const Text('Control Door'),
                        style: ElevatedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(vertical: 16),
                        ),
                        onPressed: () {
                          // Later → navigate to ControlScreen
                          ScaffoldMessenger.of(context).showSnackBar(
                            const SnackBar(
                              content: Text('Control screen coming soon'),
                            ),
                          );
                        },
                      ),

                      const SizedBox(height: 12),

                      ElevatedButton.icon(
                        icon: const Icon(Icons.history),
                        label: const Text('Event History'),
                        style: ElevatedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(vertical: 16),
                        ),
                        onPressed: () {
                          ScaffoldMessenger.of(context).showSnackBar(
                            const SnackBar(
                              content: Text('History screen coming soon'),
                            ),
                          );
                        },
                      ),
                    ],
                  ),
                ),
    );
  }

  Widget _buildInfoItem({
    required IconData icon,
    required String label,
    required String value,
  }) {
    return Column(
      children: [
        Icon(icon, size: 40, color: Colors.blue),
        const SizedBox(height: 8),
        Text(
          value,
          style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
        ),
        Text(label, style: const TextStyle(color: Colors.grey)),
      ],
    );
  }
}
