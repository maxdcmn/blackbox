#!/bin/bash
# Fix firewall to allow external access to port 6767

echo "Opening port 6767 in firewall..."

# Try ufw first
if command -v ufw &> /dev/null; then
    sudo ufw allow 6767/tcp
    sudo ufw reload
    echo "✓ ufw: Port 6767 opened"
fi

# Also add iptables rule (works even if ufw is not active)
sudo iptables -I INPUT -p tcp --dport 6767 -j ACCEPT 2>/dev/null && echo "✓ iptables: Port 6767 opened" || echo "⚠ iptables: Rule may already exist"

# Save iptables rules if possible
if command -v iptables-save &> /dev/null; then
    sudo iptables-save > /tmp/iptables_backup_$(date +%s).rules 2>/dev/null || true
fi

echo ""
echo "Verifying server is listening on all interfaces..."
sudo netstat -tlnp | grep 6767 || sudo ss -tlnp | grep 6767 || echo "⚠ Server may not be running"

echo ""
echo "Test from external machine: curl http://64.176.192.233:6767/vram"

