./ns3 build tcp-westwood-scenario1 2>/dev/null
for err in 0.01 0.05; do
    echo "Error: $err"
    echo "NewReno:"
    ./ns3 run "tcp-westwood-scenario1 --errorRate=$err --transportProt=TcpNewReno" | cut -d, -f4
    echo "WestwoodPlus (Official):"
    ./ns3 run "tcp-westwood-scenario1 --errorRate=$err --transportProt=TcpWestwoodPlus" 2>/dev/null | cut -d, -f4 
done
