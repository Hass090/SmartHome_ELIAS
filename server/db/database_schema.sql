CREATE TABLE sensors (
    id_sensors INT AUTO_INCREMENT PRIMARY KEY,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    temperature FLOAT,
    humidity FLOAT,
    pressure FLOAT
);

CREATE TABLE events (
    id_events INT AUTO_INCREMENT PRIMARY KEY,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    type VARCHAR(50),
    description VARCHAR(255)
);

CREATE TABLE access_logs (
    id_access_logs INT AUTO_INCREMENT PRIMARY KEY,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    method VARCHAR(20),
    user_name VARCHAR(50),
    status VARCHAR(20)
);
