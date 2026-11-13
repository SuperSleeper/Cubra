#include "ai/Manual.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

static void initManualResource() { Q_INIT_RESOURCE(manual); }

namespace td {

QStringList ManualTopic::targets() const {
    QStringList out;
    for (const ManualStep& step : steps)
        if (!step.target.isEmpty() && !out.contains(step.target)) out << step.target;
    for (const QString& name : also)
        if (!out.contains(name)) out << name;
    return out;
}

QString ManualTopic::text() const {
    QString out = QStringLiteral("[%1] %2\n%3\n").arg(id, title, summary);
    for (size_t i = 0; i < steps.size(); ++i) out += QStringLiteral("%1. %2\n").arg(i + 1).arg(steps[i].text);
    for (const QString& tip : tips) out += QStringLiteral("Tip: %1\n").arg(tip);
    return out;
}

std::vector<ManualTopic> parseManual(const QString& markdown) {
    QString text = markdown;
    text.remove(QRegularExpression(QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption));
    static const QRegularExpression stepLine(QStringLiteral("^\\d+\\.\\s+(.*?)\\s*(?:\\{([A-Za-z0-9_]+)\\})?\\s*$"));
    const auto list = [](const QString& value) {
        QStringList out;
        for (const QString& item : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) out << item.trimmed().toLower();
        return out;
    };

    std::vector<ManualTopic> topics;
    ManualTopic* topic = nullptr;
    for (const QString& raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("## "))) {
            topics.push_back({});
            topic = &topics.back();
            topic->title = line.mid(3).trimmed();
            continue;
        }
        if (!topic || line.isEmpty()) continue;
        if (line.startsWith(QLatin1String("id:"))) topic->id = line.mid(3).trimmed();
        else if (line.startsWith(QLatin1String("keywords:"))) topic->keywords = list(line.mid(9));
        else if (line.startsWith(QLatin1String("also:"))) {
            for (const QString& name : line.mid(5).split(QLatin1Char(','), Qt::SkipEmptyParts)) topic->also << name.trimmed();
        } else if (line.startsWith(QLatin1String("Tip:"))) topic->tips << line.mid(4).trimmed();
        else if (const auto m = stepLine.match(line); m.hasMatch()) topic->steps.push_back({m.captured(1), m.captured(2)});
        else if (topic->steps.empty()) topic->summary += (topic->summary.isEmpty() ? QString() : QStringLiteral(" ")) + line;
    }
    topics.erase(std::remove_if(topics.begin(), topics.end(), [](const ManualTopic& t) { return t.id.isEmpty() || t.title.isEmpty(); }), topics.end());
    return topics;
}

const std::vector<ManualTopic>& manual() {
    static const std::vector<ManualTopic> topics = [] {
        initManualResource();
        QFile file(QStringLiteral(":/manual.md"));
        return file.open(QIODevice::ReadOnly) ? parseManual(QString::fromUtf8(file.readAll())) : std::vector<ManualTopic>{};
    }();
    return topics;
}

const ManualTopic* manualTopic(const QString& id) {
    for (const ManualTopic& topic : manual())
        if (topic.id == id) return &topic;
    return nullptr;
}

namespace {

// Lower case, without the endings that keep "rounding" from matching "round".
QString stem(QString word) {
    word = word.toLower();
    for (const char* ending : {"ing", "ed", "es", "s"}) {
        const QLatin1String e(ending);
        if (word.size() > int(e.size()) + 3 && word.endsWith(e)) return word.left(word.size() - e.size());
    }
    return word;
}

QStringList words(const QString& text) {
    static const QSet<QString> kSkip = {"how", "do", "does", "i", "a", "an", "the", "to", "of", "in", "on", "it", "is", "can", "my", "me", "you",
                                        "what", "where", "why", "which", "and", "or", "for", "with", "this", "that", "be", "there", "are", "get",
                                        "want", "make", "use", "using", "into", "from", "at", "by", "should", "would", "could", "so", "some", "cubra"};
    static const QRegularExpression split(QStringLiteral("[^a-z0-9]+"));
    QStringList out;
    for (const QString& w : text.toLower().split(split, Qt::SkipEmptyParts))
        if (!kSkip.contains(w)) out << stem(w);
    return out;
}

}  // namespace

std::vector<const ManualTopic*> findTopics(const QString& question, int count, const std::vector<ManualTopic>& topics) {
    const QString lower = question.toLower();
    const QStringList asked = words(question);
    std::vector<std::pair<double, const ManualTopic*>> scored;
    for (const ManualTopic& topic : topics) {
        double score = 0;
        for (const QString& keyword : topic.keywords) {
            // A phrase such as "around my part" counts when it appears whole, and the longer
            // it is, the more surely it means this topic rather than one that shares a word.
            if (keyword.contains(QLatin1Char(' '))) {
                if (lower.contains(keyword)) score += 1 + 2 * (keyword.count(QLatin1Char(' ')) + 1);
            } else if (asked.contains(stem(keyword))) {
                score += 3;
            }
        }
        for (const QString& w : words(topic.title))
            if (asked.contains(w)) score += 1;
        if (score > 0) scored.push_back({score, &topic});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<const ManualTopic*> out;
    for (const auto& [score, topic] : scored)
        if (int(out.size()) < count) out.push_back(topic);
    return out;
}

bool isHelpQuestion(const QString& request) {
    const QString text = request.trimmed().toLower();
    if (text.isEmpty()) return false;
    // "Can you make a cup?" and "please make it taller" ask for a design, question mark or not.
    static const QRegularExpression design(QStringLiteral(
        "^(please\\s+)?((can|could|would|will) you\\s+(please\\s+)?)?(make|create|build|design|generate|draw|model|give me|add|put|place|turn|change|resize|move|rotate|scale)\\b"));
    if (design.match(text).hasMatch()) return false;
    static const QRegularExpression asking(QStringLiteral(
        "^(how|where|what|what's|whats|why|which|when|is there|are there|does|do i|do you|can i|could i|should i|is it|help|explain|tell me|show me|teach me|i need help|i don't know|i dont know|where's|wheres)\\b"));
    return asking.match(text).hasMatch() || text.endsWith(QLatin1Char('?'));
}

}  // namespace td
