from flask import Flask, jsonify, render_template_string
import mysql.connector

app = Flask(__name__)

# Kết nối database và lấy dữ liệu
def get_mouse_data():
    conn = mysql.connector.connect(
        host='localhost',
        user='KIETCDT24',
        password='kiet',
        database='mouse_data'
    )
    cursor = conn.cursor(dictionary=True)
    cursor.execute("SELECT x, y, timestamp FROM mouse_data ORDER BY id ASC")
    data = cursor.fetchall()
    cursor.close()
    conn.close()
    return data

# Giao diện web, viết inline hết trong Python luôn
HTML_PAGE = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Mouse Trajectory Viewer</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { text-align: center; font-family: Arial, sans-serif; background-color: #f5f5f5; }
        canvas { max-width: 80%; margin-top: 20px; }
    </style>
</head>
<body>
    <h2>🖱️ Quỹ Đạo Chuột từ SQL Database</h2>
    <canvas id="trajectoryChart"></canvas>

    <script>
    fetch('/data')
        .then(response => response.json())
        .then(data => {
            const points = data.map(point => ({ x: point.x, y: point.y }));
            const ctx = document.getElementById('trajectoryChart').getContext('2d');

            new Chart(ctx, {
                type: 'scatter',
                data: {
                    datasets: [{
                        label: 'Mouse Trajectory',
                        data: points,
                        showLine: true,
                        borderColor: 'rgba(75, 192, 192, 1)',
                        backgroundColor: 'rgba(75, 192, 192, 0.2)',
                        pointRadius: 3,
                        tension: 0.3
                    }]
                },
                options: {
                    scales: {
                        x: { title: { display: true, text: 'X' } },
                        y: { title: { display: true, text: 'Y' } }
                    }
                }
            });
        });
    </script>
</body>
</html>
"""

@app.route('/')
def index():
    return render_template_string(HTML_PAGE)

@app.route('/data')
def data():
    return jsonify(get_mouse_data())

if __name__ == '__main__':
    app.run(debug=True)

