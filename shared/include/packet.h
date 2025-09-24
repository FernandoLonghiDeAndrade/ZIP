class Packet {
public:
    Packet(bool seq_number, char data[256]);
    int seq_number = -1;
    char data[256];
};